#include "pcmk_pe_ssh.h"

#include <glib.h>
#include <qb/qbdefs.h>
#include <sys/epoll.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <qb/qbmap.h>
#include <libxml/parser.h>
#include <libxslt/transform.h>
#include <libdeltacloud/libdeltacloud.h>
#include <libssh2.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#define INSTANCE_STATE_OFFLINE 1
#define INSTANCE_STATE_PENDING 2
#define INSTANCE_STATE_RUNNING 3
#define INSTANCE_STATE_FAILED 4

#define SSH_STATE_UNDEFINED		0
#define SSH_STATE_CONNECT		1
#define SSH_STATE_CONNECTING		2
#define SSH_STATE_SESSION_STARTUP	3
#define SSH_STATE_SESSION_KEYSETUP	4
#define SSH_STATE_SESSION_OPENSESSION	5
#define SSH_STATE_EXEC			6
#define SSH_STATE_READ			7

extern qb_loop_t* mainloop;

static qb_loop_timer_handle timer_handle;

static qb_loop_timer_handle timer_processing;

static qb_map_t *assemblies;

static xmlNode *policy_root;

static xmlDoc *policy;

struct resource {
	char *name;
};

struct assembly {
	char *name;
	char *uuid;
	char *address;
	int state;
	int ssh_state;
	qb_map_t *resources;
	char *instance_id;
	int healthcheck_failures;
	int fd;
	char time[32];
	qb_loop_timer_handle healthcheck_timer;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	struct sockaddr_in sin;
};

static void assembly_state_changed(struct assembly *assembly, int state);

static int32_t instance_create(struct assembly *assembly);

static void connect_execute(void *data);

static void resource_execute_cb(struct pe_operation *op) {
	printf("execute resource cb\n");
}

static void transition_completed_cb(void* user_data, int32_t result) {
	printf("transition completed cb\n");
}

static const char *my_tags_stringify(uint32_t tags)
{
        if (qb_bit_is_set(tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
                return "QB   ";
        } else if (tags == 1) {
                return "QPID ";
        } else if (tags == 2) {
                return "GLIB ";
        } else if (tags == 3) {
                return "PCMK ";
        } else {
                return "MAIN ";
        }
}

static void
my_glib_handler(const gchar *log_domain, GLogLevelFlags flags, const gchar *message, gpointer user_data)
{
	uint32_t log_level = LOG_WARNING;
	GLogLevelFlags msg_level = (GLogLevelFlags)(flags & G_LOG_LEVEL_MASK);

	switch (msg_level) {
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_FLAG_FATAL:
		log_level = LOG_CRIT;
		break;
	case G_LOG_LEVEL_ERROR:
		log_level = LOG_ERR;
		break;
	case G_LOG_LEVEL_MESSAGE:
		log_level = LOG_NOTICE;
		break;
	case G_LOG_LEVEL_INFO:
		log_level = LOG_INFO;
		break;
	case G_LOG_LEVEL_DEBUG:
		log_level = LOG_DEBUG;
		break;

	case G_LOG_LEVEL_WARNING:
	case G_LOG_FLAG_RECURSION:
	case G_LOG_LEVEL_MASK:
		log_level = LOG_WARNING;
		break;
	}

	qb_log_from_external_source(__FUNCTION__, __FILE__, "%s",
				    log_level, __LINE__,
				    2, message);
}

static int instance_stop(char *image_name)
{
	static struct deltacloud_api api;
	struct deltacloud_instance *instances = NULL;
	struct deltacloud_instance *instances_head = NULL;
	struct deltacloud_image *images_head;
	struct deltacloud_image *images;
	int rc;

	if (deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "") < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}
	if (deltacloud_get_instances(&api, &instances) < 0) {
	fprintf(stderr, "Failed to get deltacloud instances: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}

	rc = deltacloud_get_images(&api, &images);
	images_head = images;
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}

	instances_head = instances;
	for (; images; images = images->next) {
		for (instances = instances_head; instances; instances = instances->next) {
			if (strcmp(images->name, image_name) == 0) {
				if (strcmp(instances->image_id, images->id) == 0) {
					deltacloud_instance_stop(&api, instances);
					break;
				}
			}
		}
	}

	deltacloud_free_image_list(&images_head);
	deltacloud_free_instance_list(&instances_head);
	deltacloud_free(&api);
	return 0;
}

static void insert_status(xmlNode *status, struct assembly *assembly)
{
	qb_log(LOG_INFO, "Inserting assembly %s", assembly->name);
	xmlNode *node_state = xmlNewChild(status, NULL, "node_state", NULL);
        xmlNewProp(node_state, "id", assembly->uuid);
        xmlNewProp(node_state, "uname", assembly->name);
        xmlNewProp(node_state, "ha", BAD_CAST "active");
        xmlNewProp(node_state, "expected", BAD_CAST "member");
        xmlNewProp(node_state, "in_ccm", BAD_CAST "true");
        xmlNewProp(node_state, "crmd", BAD_CAST "online");

	/* check state*/
	xmlNewProp(node_state, "join", "member");
}

static void schedule_processing(void);

static void process(void)
{
	xmlNode *cur_node;
	xmlNode *status;
	int rc;
	struct assembly *assembly;
	qb_map_iter_t *iter;
	const char *p;
	size_t res;
	const char *key;

	/*
	 * Remove status descriptor
	 */
	policy_root = xmlDocGetRootElement(policy);
	for (cur_node = policy_root->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE &&
			strcmp((char*)cur_node->name, "status") == 0) {

			xmlUnlinkNode(cur_node);
			xmlFreeNode(cur_node);
			break;
		}
	}

	status = xmlNewChild(policy_root, NULL, "status", NULL);
	iter = qb_map_iter_create(assemblies);
	while ((key = qb_map_iter_next(iter, (void **)&assembly)) != NULL) {
		insert_status(status, assembly);
	}
	qb_map_iter_free(iter);

	rc = pe_process_state(policy_root, resource_execute_cb,
		transition_completed_cb,  NULL);
	if (rc != 0) {
		schedule_processing();
	}
}

static void status_timeout(void *data)
{
	char *instance_id = (char *)data;

	if (pe_is_busy_processing()) {
		schedule_processing();
	} else {
		process();
	}
}

static void schedule_processing(void)
{
        if (qb_loop_timer_expire_time_get(mainloop, timer_processing) > 0) {
		qb_log(LOG_DEBUG, "not scheduling - already scheduled");
	} else {
		qb_loop_timer_add(mainloop, QB_LOOP_LOW,
			1000 * QB_TIME_NS_IN_MSEC, NULL,
			status_timeout, &timer_processing);
	}
}

static void assembly_healthcheck(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	char buffer[1024];
	int rc;
	int i;
	char *end;

	assembly->channel = libssh2_channel_open_session(assembly->session);
	if (assembly->channel == NULL) {
		qb_log(LOG_NOTICE,
			"open session failed %d\n",
			libssh2_session_last_errno(assembly->session));
		goto error;
	}

	rc = libssh2_channel_exec(assembly->channel, "uptime");
	if (rc != 0) {
		qb_log(LOG_NOTICE,
			"channel exec failed %d\n", rc);
		goto error;
	}
		
	rc = libssh2_channel_read(assembly->channel, buffer, sizeof(buffer));
	
error:
	end = strstr (buffer, " up");
	if (end) {
		*end = '\0';
	
		if (strcmp (buffer, assembly->time) == 0) {
			assembly->healthcheck_failures += 1;
		} else {
			assembly->healthcheck_failures = 0;
			strcpy (assembly->time, buffer);
		}
	}

	if (end == NULL || rc != 0) {
		assembly->healthcheck_failures += 1;
	}

	if (assembly->healthcheck_failures > 3) {
		qb_log(LOG_NOTICE, "Executing a restart for assembly '%s'",
			assembly->name);

		assembly_state_changed(assembly, INSTANCE_STATE_FAILED);
		return;
	}

	rc = libssh2_channel_close(assembly->channel);
	if (rc != 0) {
		qb_log(LOG_NOTICE,
			"channel close failed %d\n", rc);
	}

	qb_loop_timer_add(mainloop, QB_LOOP_HIGH,
		3000 * QB_TIME_NS_IN_MSEC, assembly,
		assembly_healthcheck, NULL);
}

static void ssh_assembly_connect(struct assembly *assembly)
{
	char name[1024];
	char name_pub[1024];
	int i;
	int rc;

	assembly->session = libssh2_session_init();
//	libssh2_session_set_blocking(assembly->session, 0);
	rc = libssh2_session_startup(assembly->session, assembly->fd);
	if (rc != 0) {
		qb_log(LOG_NOTICE,
			"session startup failed %d\n", rc);
		goto error;
	}

	libssh2_keepalive_config(assembly->session, 1, 2);

	sprintf (name, "/var/lib/pacemaker-cloud/keys/%s",
		assembly->name);
	sprintf (name_pub, "/var/lib/pacemaker-cloud/keys/%s.pub",
		assembly->name);
	rc = libssh2_userauth_publickey_fromfile(assembly->session,
		"root", name_pub, name, "");
	if (rc != 0) {
		qb_log(LOG_NOTICE,
			"userauth_publickey_fromfile failed %d\n", rc);
		goto error;
	}
	if (rc) {
		qb_log(LOG_ERR,
			"Authentication by public key for '%s' failed\n",
			assembly->name);
		/*
		 * TODO give this another go
		 */
	} else {
		qb_log(LOG_NOTICE,
			"Authentication by public key for '%s' successful\n",
			assembly->name);
		assembly_healthcheck(assembly);
	}

	assembly_state_changed(assembly, INSTANCE_STATE_RUNNING);
error:
	return;
}

static void connect_execute(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	int rc;

	rc = connect(assembly->fd, (struct sockaddr*)(&assembly->sin),
		sizeof (struct sockaddr_in));
	if (rc == 0) {
		qb_log(LOG_NOTICE, "Connected to assembly '%s'",
			assembly->name);
		ssh_assembly_connect (assembly);
	} else {
		qb_loop_job_add(mainloop, QB_LOOP_LOW, assembly, connect_execute);
	}
}

static void assembly_connect(struct assembly *assembly)
{
	unsigned long hostaddr;
	int rc;

	hostaddr = inet_addr(assembly->address);
	assembly->fd = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(assembly->fd, O_NONBLOCK);

	assembly->sin.sin_family = AF_INET;
	assembly->sin.sin_port = htons(22);
	assembly->sin.sin_addr.s_addr = hostaddr;
	assembly->ssh_state = SSH_STATE_CONNECT;

	qb_log(LOG_NOTICE, "Connection in progress to assembly '%s'",
		assembly->name);

	qb_loop_job_add(mainloop, QB_LOOP_LOW, assembly, connect_execute);
}

static void assembly_state_changed(struct assembly *assembly, int state)
{
	if (state == INSTANCE_STATE_FAILED) {
		instance_create(assembly);
	}
	schedule_processing();
}

static void instance_state_detect(void *data)
{
	static struct deltacloud_api api;
	struct assembly *assembly = (struct assembly *)data;
	struct deltacloud_instance instance;
	struct deltacloud_instance *instance_p;
	int rc;
	int i;
	char *sptr;
	char *sptr_end;

	if (deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "") < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return;
	}
	
	rc = deltacloud_get_instance_by_id(&api, assembly->instance_id, &instance);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return;
	}


	if (strcmp(instance.state, "RUNNING") == 0) {
		for (i = 0, instance_p = &instance; instance_p; instance_p = instance_p->next, i++) {
			/*
			 * Eliminate the garbage output of this DC api
			 */
			sptr = instance_p->private_addresses->address +
				strspn (instance_p->private_addresses->address, " \t\n");
			sptr_end = sptr + strcspn (sptr, " \t\n");
			*sptr_end = '\0';
		}
		
		assembly->address = strdup (sptr);
		qb_log(LOG_INFO, "Instance '%s' changed to RUNNING.",
			assembly->name);
		assembly_connect(assembly);
	} else
	if (strcmp(instance.state, "PENDING") == 0) {
		qb_log(LOG_INFO, "Instance '%s' is PENDING.",
			assembly->name);
		qb_loop_timer_add(mainloop, QB_LOOP_LOW,
			1000 * QB_TIME_NS_IN_MSEC, assembly,
			instance_state_detect, &timer_handle);
	}
}

static int32_t instance_create(struct assembly *assembly)
{
	static struct deltacloud_api api;
	struct deltacloud_image *images_head;
	struct deltacloud_image *images;
	int rc;

	if (deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "") < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}
	rc = deltacloud_get_images(&api, &images);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}

	for (images_head = images; images; images = images->next) {
		if (strcmp(images->name, assembly->name) == 0) {
			rc = deltacloud_create_instance(&api, images->id, NULL, 0, &assembly->instance_id);
			printf ("creating instance id %s\n", assembly->instance_id);
			if (rc < 0) {
				fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
					deltacloud_get_last_error_string());
				return -1;
			}
			instance_state_detect(assembly);
		}
	}
	deltacloud_free_image_list(&images_head);
	deltacloud_free(&api);
	return 0;
}

static void resource_create(xmlNode *cur_node, struct assembly *assembly)
{
	struct resource *resource;
	char *name;

	resource = calloc (1, sizeof (struct resource));
	name = xmlGetProp(cur_node, "name");
	resource->name = strdup(name);
	qb_map_put(assembly->resources, name, assembly);
}

static void resources_create(xmlNode *cur_node, struct assembly *assembly)
{

	
	for (; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		resource_create(cur_node, assembly);
	}
}

static void assembly_create(xmlNode *cur_node)
{
	struct assembly *assembly;
	char *name;
	char *uuid;
	xmlNode *child_node;
	size_t res;
	
	assembly = calloc(1, sizeof (struct assembly));
	name = xmlGetProp(cur_node, "name");
	assembly->name = strdup(name);
	uuid = xmlGetProp(cur_node, "uuid");
	assembly->uuid = strdup(uuid);
	assembly->state = INSTANCE_STATE_OFFLINE;
	assembly->resources = qb_skiplist_create();
	instance_create(assembly);
	qb_map_put(assemblies, name, assembly);

	for (child_node = cur_node->children; child_node;
		child_node = child_node->next) {
		if (child_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (strcmp(child_node->name, "services") == 0) {
			resources_create(child_node->children, assembly);
		}
	}
}

static void assemblies_create(xmlNode *xml)
{
	struct resource *resource;
	xmlNode *cur_node;

	char *ass_name;

        for (cur_node = xml; cur_node; cur_node = cur_node->next) {
                if (cur_node->type != XML_ELEMENT_NODE) {
                        continue;
                }
		assembly_create(cur_node);
	}
}

static void stop_assemblies(xmlNode *assemblies)
{
	xmlNode *cur_node;

	char *ass_name;

        for (cur_node = assemblies; cur_node; cur_node = cur_node->next) {
                if (cur_node->type != XML_ELEMENT_NODE) {
                        continue;
                }
                ass_name = (char*)xmlGetProp(cur_node, BAD_CAST "name");
		instance_stop(ass_name);
	}
}


int main (void)
{
	xmlDoc *original_config;
	xmlNode *cur_node;
	xmlNode *dep_node;
        xsltStylesheetPtr ss;
	const char *params[1];
	int daemonize = 0;
	int rc;

	int loglevel = LOG_INFO;

	qb_log_init("dped", LOG_DAEMON, loglevel);
	qb_log_format_set(QB_LOG_SYSLOG, "%g[%p] %b");
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, LOG_INFO - loglevel);
	if (!daemonize) {
		qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
		QB_LOG_FILTER_FILE, "*", loglevel);
		qb_log_format_set(QB_LOG_STDERR, "%g[%p] %b");
		qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	}
	qb_log_tags_stringify_fn_set(my_tags_stringify);

        g_log_set_default_handler(my_glib_handler, NULL);

        mainloop = qb_loop_create();

	rc = libssh2_init(0);
	if (rc != 0) {
		qb_log(LOG_CRIT, "libssh2 initialization failed (%d).", rc);
		return 1;	
	}
	params[0] = NULL;

	original_config = xmlParseFile("/var/run/dep-wp.xml");
	ss = xsltParseStylesheetFile(BAD_CAST "/usr/share/pacemaker-cloud/cf2pe.xsl");

        policy = xsltApplyStylesheet(ss, original_config, params);

        xsltFreeStylesheet(ss);

        dep_node = xmlDocGetRootElement(original_config);

	assemblies = qb_skiplist_create();
        for (cur_node = dep_node->children; cur_node;
             cur_node = cur_node->next) {
                if (cur_node->type == XML_ELEMENT_NODE) {
                        if (strcmp((char*)cur_node->name, "assemblies") == 0) {
				assemblies_create(cur_node->children);
                        }
                }
        }
	policy_root = xmlDocGetRootElement(policy);
	pe_process_state(policy_root, resource_execute_cb,
		transition_completed_cb, NULL);

#ifdef COMPILE_OUT
	printf ("stopping instances\n");
        for (cur_node = dep_node->children; cur_node;
             cur_node = cur_node->next) {
                if (cur_node->type == XML_ELEMENT_NODE) {
                        if (strcmp((char*)cur_node->name, "assemblies") == 0) {
                                stop_assemblies(cur_node->children);
                        }
                }
        }
#endif

	qb_loop_run(mainloop);
	return 0;
}
