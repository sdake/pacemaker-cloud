#include "pcmk_pe.h"

#include <sys/epoll.h>
#include <glib.h>
#include <uuid/uuid.h>
#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <qb/qbmap.h>
#include <libxml/parser.h>
#include <libxslt/transform.h>
#include <libdeltacloud/libdeltacloud.h>
#include <libssh2.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>

#define KEEPALIVE_TIMEOUT 15		/* seconds */
#define SSH_TIMEOUT 5000		/* milliseconds */
#define SCHEDULE_PROCESS_TIMEOUT 1000	/* milliseconds */
#define PENDING_TIMEOUT 1000		/* milliseconds */
#define HEALTHCHECK_TIMEOUT 3000	/* milliseconds */

enum instance_state {
	INSTANCE_STATE_OFFLINE = 1,
	INSTANCE_STATE_PENDING = 2,
	INSTANCE_STATE_RUNNING = 3,
	INSTANCE_STATE_FAILED = 4,
	INSTANCE_STATE_RECOVERING = 5
};

enum ssh_state {
	SSH_SESSION_INIT = 0,
	SSH_SESSION_STARTUP = 1,
	SSH_KEEPALIVE_CONFIG = 2,
	SSH_USERAUTH_PUBLICKEY_FROMFILE = 3,
	SSH_CONNECTED = 4
};

enum ssh_exec_state {
	SSH_CHANNEL_OPEN = 0,
	SSH_CHANNEL_EXEC = 1,
	SSH_CHANNEL_READ = 2,
	SSH_CHANNEL_CLOSE = 3,
	SSH_CHANNEL_WAIT_CLOSED = 4,
	SSH_CHANNEL_FREE = 5
};

static qb_loop_t* mainloop;

static qb_loop_timer_handle timer_handle;

static qb_loop_timer_handle timer_processing;

static qb_map_t *assembly_map;

static qb_map_t *op_history_map;

static int call_order = 0;

static int counter = 0;

static char crmd_uuid[37];

static xmlDocPtr _pe = NULL;

static xmlDocPtr _config = NULL;

struct operation_history {
	char *rsc_id;
	char *operation;
	uint32_t call_id;
	uint32_t interval;
	enum ocf_exitcode rc;
	uint32_t target_outcome;
	time_t last_run;
	time_t last_rc_change;
	uint32_t graph_id;
	uint32_t action_id;
	char *op_digest;
	struct resource *resource;
};

struct assembly {
	char *name;
	char *uuid;
	char *address;
	char *instance_id;
	enum instance_state instance_state;
	struct qb_list_head ssh_op_head;
	enum ssh_state ssh_state;
	qb_map_t *resource_map;
	int fd;
	qb_loop_timer_handle healthcheck_timer;
	qb_loop_timer_handle keepalive_timer;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	struct sockaddr_in sin;
};

struct resource {
	char *name;
	char *type;
	char *class;
	struct assembly *assembly;
	qb_loop_timer_handle monitor_timer;
};

struct ssh_operation {
	int ssh_rc;
	struct assembly *assembly;
	struct resource *resource;
	struct qb_list_head list;
	struct pe_operation *op;
	qb_loop_timer_handle ssh_timer;
	enum ssh_exec_state ssh_exec_state;
	qb_loop_job_dispatch_fn completion_func;
	LIBSSH2_CHANNEL *channel;
	char command[4096];
};

static void assembly_state_changed(struct assembly *assembly, int state);

static int32_t instance_create(struct assembly *assembly);

static void connect_execute(void *data);

static void resource_monitor_execute(struct pe_operation *op);

static void schedule_processing(void);

static void assembly_healthcheck(void *data);

static void op_history_save(struct resource *resource, struct pe_operation *op,
	enum ocf_exitcode ec)
{
	struct operation_history *oh;
	char buffer[4096];

	sprintf(buffer, "%s_%s_%d", op->rname, op->method, op->interval);

	oh = qb_map_get(op_history_map, buffer);
	if (oh == NULL) {
		oh = (struct operation_history *)calloc(1, sizeof(struct operation_history));
		oh->resource = resource;
		oh->rsc_id = strdup(buffer);
		oh->operation = strdup(op->method);
		oh->target_outcome = op->target_outcome;
		oh->interval = op->interval;
		oh->rc = OCF_PENDING;
		oh->op_digest = op->op_digest;
		qb_map_put(op_history_map, oh->rsc_id, oh);
	} else
	if (strcmp(oh->op_digest, op->op_digest) != 0) {
		free(oh->op_digest);
		oh->op_digest = op->op_digest;
	}
        if (oh->rc != ec) {
                oh->last_rc_change = time(NULL);
                oh->rc = ec;
        }

        oh->last_run = time(NULL);
        oh->call_id = call_order++;
        oh->graph_id = op->graph_id;
        oh->action_id = op->action_id;
}

static void xml_new_int_prop(xmlNode *n, const char *name, int32_t val)
{
	char int_str[36];
	snprintf(int_str, 36, "%d", val);
	xmlNewProp(n, BAD_CAST name, BAD_CAST int_str);
}

static void xml_new_time_prop(xmlNode *n, const char *name, time_t val)
{
        char int_str[36];
        snprintf(int_str, 36, "%d", val);
        xmlNewProp(n, BAD_CAST name, BAD_CAST int_str);
}



static void op_history_insert(xmlNode *resource_xml,
	struct operation_history *oh)
{
	xmlNode *op;
	char key[255];
	char magic[255];

	op = xmlNewChild(resource_xml, NULL, "lrm_rsc_op", NULL);

	xmlNewProp(op, "id", oh->rsc_id);
	xmlNewProp(op, "operation", oh->operation);
	xml_new_int_prop(op, "call-id", oh->call_id);
	xml_new_int_prop(op, "rc-code", oh->rc);
	xml_new_int_prop(op, "interval", oh->interval);
	xml_new_time_prop(op, "last-run", oh->last_run);
	xml_new_time_prop(op, "last-rc-change", oh->last_rc_change);

	snprintf(key, 255, "%d:%d:%d:%s",
		oh->action_id, oh->graph_id, oh->target_outcome, crmd_uuid);

	xmlNewProp(op, "transition-key", key);

	snprintf(magic, 255, "0:%d:%s", oh->rc, key);
	xmlNewProp(op, "transition-magic", magic);

	xmlNewProp(op, "op-digest", oh->op_digest);
	xmlNewProp(op, "crm-debug-origin", __func__);
	xmlNewProp(op, "crm_feature_set", PE_CRM_VERSION);
	xmlNewProp(op, "op-status", "0");
	xmlNewProp(op, "exec-time", "0");
	xmlNewProp(op, "queue-time","0");
}


static void monitor_timeout(void *data)
{
	struct pe_operation *op = (struct pe_operation *)data;

	resource_monitor_execute(op);
}	

static void assembly_ssh_exec(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	int rc;
	int rc_close;
	char *errmsg;
	int size;
	char buffer[4096];

	switch (ssh_op->ssh_exec_state) {
	case SSH_CHANNEL_OPEN:
		ssh_op->channel = libssh2_channel_open_session(ssh_op->assembly->session);
		if (ssh_op->channel == NULL) {
			rc = libssh2_session_last_errno(ssh_op->assembly->session);
			if (rc == LIBSSH2_ERROR_EAGAIN) {
				goto job_repeat_schedule;
			}
			qb_log(LOG_NOTICE,
				"open session failed %d\n", rc);
			return;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_EXEC;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_EXEC:
		rc = libssh2_channel_exec(ssh_op->channel, ssh_op->command);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_exec failed %d\n", rc);
			goto error_close;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_READ;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_READ:
		rc = libssh2_channel_read(ssh_op->channel, buffer, sizeof(buffer));
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc < 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_read failed %d\n", rc);
			goto error_close;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_CLOSE;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_CLOSE:
		rc_close = libssh2_channel_close(ssh_op->channel);
		if (rc_close == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc_close != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel close failed %d\n", rc_close);
			return;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_WAIT_CLOSED;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_WAIT_CLOSED:
		rc_close = libssh2_channel_wait_closed(ssh_op->channel);
		if (rc_close == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc_close != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_wait_closed failed %d\n", rc_close);
			return;
		}
		ssh_op->ssh_rc = libssh2_channel_get_exit_status(ssh_op->channel);
		ssh_op->ssh_exec_state = SSH_CHANNEL_FREE;

		/*
                 * no break here is intentional
		 */

error_close:
	case SSH_CHANNEL_FREE:
		rc = libssh2_channel_free(ssh_op->channel);
		if (rc < 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_free failed %d\n", rc);
		}
		/*
                 * no break here is intentional
		 */
	} /* switch */

	qb_loop_timer_del(mainloop, ssh_op->ssh_timer);
	qb_loop_job_add(mainloop, QB_LOOP_LOW, ssh_op, ssh_op->completion_func);
	qb_list_del(&ssh_op->list);
	return;

job_repeat_schedule:
	qb_loop_job_add(mainloop, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
}

static void assembly_state_changed(struct assembly *assembly, int state)
{
	if (state == INSTANCE_STATE_FAILED) {
		qb_loop_timer_del(mainloop, assembly->keepalive_timer);
		qb_loop_timer_del(mainloop, assembly->healthcheck_timer);
		instance_create(assembly);
	}
	assembly->instance_state = state;
	schedule_processing();
}

static void service_state_changed(struct assembly *assembly,
	char *hostname, char *resource, char *state, char *reason)
{
	assembly_state_changed(assembly, INSTANCE_STATE_RECOVERING);
}
static void resource_failed(struct pe_operation *op)
{
	struct resource *resource = (struct resource *)op->resource;
	service_state_changed(resource->assembly, op->hostname, op->rtype,
		"failed", "monitor failed");
	qb_loop_timer_del(mainloop, resource->monitor_timer);
}


static void resource_monitor_completion(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	int pe_exitcode;

	pe_exitcode = pe_resource_ocf_exitcode_get(ssh_op->op, ssh_op->ssh_rc);
	qb_log(LOG_INFO,
		"Monitoring resource '%s' on assembly '%s' ocf code '%d'\n",
		ssh_op->op->rname, ssh_op->op->hostname, pe_exitcode);
	if (strstr(ssh_op->op->rname, ssh_op->op->hostname) != NULL) {
		op_history_save(ssh_op->resource, ssh_op->op, pe_exitcode);
	}
	pe_resource_completed(ssh_op->op, pe_exitcode);
	if (pe_exitcode != ssh_op->op->target_outcome) {
		resource_failed(ssh_op->op);
	}
	qb_loop_timer_add(mainloop, QB_LOOP_LOW,
		ssh_op->op->interval * QB_TIME_NS_IN_MSEC, ssh_op->op, monitor_timeout,
		&ssh_op->resource->monitor_timer);
	//free(ssh_op);
}

static void ssh_timeout(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	struct qb_list_head *list_temp;
	struct qb_list_head *list;
	struct ssh_operation *ssh_op_del;

	qb_log(LOG_NOTICE, "ssh service timeout on assembly '%s'", ssh_op->assembly->name);
	qb_list_for_each_safe(list, list_temp, &ssh_op->assembly->ssh_op_head) {
		ssh_op_del = qb_list_entry(list, struct ssh_operation, list);
		qb_loop_timer_del(mainloop, ssh_op_del->ssh_timer);
		qb_loop_job_del(mainloop, QB_LOOP_LOW, ssh_op_del,
			assembly_ssh_exec);
		if (ssh_op_del->resource) {
			qb_loop_timer_del(mainloop, ssh_op_del->resource->monitor_timer);
		}
		qb_list_del(list);
		qb_log(LOG_NOTICE, "delete ssh operation '%s'", ssh_op_del->command);
	}

	assembly_state_changed(ssh_op->assembly, INSTANCE_STATE_FAILED);
}

static void ssh_nonblocking_exec(struct assembly *assembly,
	struct resource *resource, struct pe_operation *op,
	void (*completion_func)(void *),
	char *format, ...)
{
	va_list ap;
	struct ssh_operation *ssh_op;

	ssh_op = calloc(1, sizeof(struct ssh_operation));

	va_start(ap, format);
	vsprintf(ssh_op->command, format, ap);
	va_end(ap);

	qb_log(LOG_NOTICE, "ssh_exec for assembly '%s' command '%s'",
		assembly->name, ssh_op->command);

	ssh_op->assembly = assembly;
	ssh_op->ssh_rc = 0;
	ssh_op->op = op;
	ssh_op->ssh_exec_state = SSH_CHANNEL_OPEN;
	ssh_op->resource = resource;
	ssh_op->completion_func = completion_func;
	qb_list_init(&ssh_op->list);
	qb_list_add_tail(&ssh_op->list, &ssh_op->assembly->ssh_op_head);
	qb_loop_job_add(mainloop, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
	qb_loop_timer_add(mainloop, QB_LOOP_LOW,
		 SSH_TIMEOUT * QB_TIME_NS_IN_MSEC,
		ssh_op, ssh_timeout, &ssh_op->ssh_timer);
}

static void resource_monitor_execute(struct pe_operation *op)
{
	struct assembly *assembly;
	struct resource *resource;
 
	assembly = qb_map_get(assembly_map, op->hostname);
	resource = qb_map_get(assembly->resource_map, op->rname);

	ssh_nonblocking_exec(assembly, resource, op,
		resource_monitor_completion,
		"systemctl status %s.service", op->rtype);
}

static void op_history_delete(struct pe_operation *op)
{
	struct resource *resource;
	qb_map_iter_t *iter;
	const char *key;
	struct operation_history *oh;

	/*
	 * Delete this resource from any operational histories
	 */
	iter = qb_map_iter_create(op_history_map);
	while ((key = qb_map_iter_next(iter, (void **)&oh)) != NULL) {
		resource = (struct resource *)oh->resource;
	
		if (resource == op->resource) {
			qb_map_rm(op_history_map, key);
			free(oh->rsc_id);
			free(oh->operation);
			free(oh);
		}
	}

	pe_resource_completed(op, OCF_OK);
}

static void ssh_start_completion(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	int pe_exitcode;

	pe_exitcode = pe_resource_ocf_exitcode_get(ssh_op->op, ssh_op->ssh_rc);
	qb_log(LOG_INFO,
		"Starting resource '%s' on assembly '%s' ocf code '%d'\n",
		ssh_op->op->rname, ssh_op->op->hostname, pe_exitcode);
	pe_resource_completed(ssh_op->op, pe_exitcode);
}

static void ssh_stop_completion(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	int pe_exitcode;

	pe_exitcode = pe_resource_ocf_exitcode_get(ssh_op->op, ssh_op->ssh_rc);
	qb_log(LOG_INFO,
		"Stopping resource '%s' on assembly '%s' ocf code '%d'\n",
		ssh_op->op->rname, ssh_op->op->hostname, pe_exitcode);
		qb_loop_timer_del(mainloop, ssh_op->resource->monitor_timer);
	pe_resource_completed(ssh_op->op, pe_exitcode);
}

static void resource_execute_cb(struct pe_operation *op)
{
	struct resource *resource;
	struct assembly *assembly;
	int rc;
	int ssh_rc;

	assembly = qb_map_get(assembly_map, op->hostname);
	resource = qb_map_get(assembly->resource_map, op->rname);

	qb_log(LOG_NOTICE, "resource_execute_cb method '%s' name '%s' interval '%d'",
		op->method, op->rname, op->interval);
	if (strcmp(op->method, "monitor") == 0) {
		if (strstr(op->rname, op->hostname) != NULL) {
			op->resource = resource;
			assert(op->resource);
			if (op->interval > 0) {
				op_history_delete(op);
				qb_loop_timer_add(mainloop, QB_LOOP_LOW,
					op->interval * QB_TIME_NS_IN_MSEC, op,
					monitor_timeout,
					&resource->monitor_timer);
			} else {
				resource_monitor_execute(op);
			}
		} else {
			pe_resource_completed(op, OCF_NOT_RUNNING);
		}
	} else
	if (strcmp (op->method, "start") == 0) {
		ssh_nonblocking_exec(assembly, resource, op,
			ssh_start_completion,
			"systemctl start %s.service", op->rtype);

	} else
	if (strcmp(op->method, "stop") == 0) {
		ssh_nonblocking_exec(assembly, resource, op,
			ssh_start_completion,
			"systemctl stop %s.service", op->rtype);
	} else
	if (strcmp(op->method, "delete") == 0) {
		qb_loop_timer_del(mainloop, resource->monitor_timer);
		op_history_delete(op);
	} else {
		assert(0);
	}
}

static void transition_completed_cb(void* user_data, int32_t result) {
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
static xmlNode *insert_resource(xmlNode *status, struct resource *resource)
{
	xmlNode *resource_xml;

	resource_xml = xmlNewChild (status, NULL, "lrm_resource", NULL);
	xmlNewProp(resource_xml, "id", resource->name);
	xmlNewProp(resource_xml, "type", resource->type);
	xmlNewProp(resource_xml, "class", resource->class);

	return resource_xml;
}

static void insert_status(xmlNode *status, struct assembly *assembly)
{
	struct operation_history *oh;
	xmlNode *resource_xml;
	xmlNode *resources_xml;
	xmlNode *lrm_xml;
	struct resource *resource;
	qb_map_iter_t *iter;
	const char *key;

	qb_log(LOG_INFO, "Inserting assembly %s", assembly->name);
		
	xmlNode *node_state = xmlNewChild(status, NULL, "node_state", NULL);
        xmlNewProp(node_state, "id", assembly->uuid);
        xmlNewProp(node_state, "uname", assembly->name);
        xmlNewProp(node_state, "ha", "active");
        xmlNewProp(node_state, "expected", "member");
        xmlNewProp(node_state, "in_ccm", "true");
        xmlNewProp(node_state, "crmd", "online");

	/* check state*/
	xmlNewProp(node_state, "join", "member");
	lrm_xml = xmlNewChild(node_state, NULL, "lrm", NULL);
	resources_xml = xmlNewChild(lrm_xml, NULL, "lrm_resources", NULL);
	iter = qb_map_iter_create(op_history_map);
	while ((key = qb_map_iter_next(iter, (void **)&oh)) != NULL) {
		resource = oh->resource;
	
		if (strstr(resource->name, assembly->name) == NULL) {
			continue;
		}
		resource_xml = insert_resource(resources_xml, oh->resource);
		op_history_insert(resource_xml, oh);
	}
	
}

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
	static xmlNode *pe_root;
	char filename[1024];


	/*
	 * Remove status descriptor
	 */
	pe_root = xmlDocGetRootElement(_pe);
	for (cur_node = pe_root->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE &&
			strcmp((char*)cur_node->name, "status") == 0) {

			xmlUnlinkNode(cur_node);
			xmlFreeNode(cur_node);
			break;
		}
	}

	status = xmlNewChild(pe_root, NULL, "status", NULL);
	iter = qb_map_iter_create(assembly_map);
	while ((key = qb_map_iter_next(iter, (void **)&assembly)) != NULL) {
		insert_status(status, assembly);
	}
	qb_map_iter_free(iter);

	rc = pe_process_state(pe_root, resource_execute_cb,
		transition_completed_cb,  NULL);
	sprintf (filename, "/tmp/z%d.xml", counter++);
	xmlSaveFormatFileEnc(filename, _pe, "UTF-8", 1);
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
			SCHEDULE_PROCESS_TIMEOUT * QB_TIME_NS_IN_MSEC, NULL,
			status_timeout, &timer_processing);
	}
}

static void assembly_healthcheck_completion(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;

	qb_log(LOG_NOTICE, "assembly_healthcheck_completion for assembly '%s'", ssh_op->assembly->name);
	if (ssh_op->ssh_rc != 0) {
		qb_log(LOG_NOTICE, "assembly healthcheck failed %d\n", ssh_op->ssh_rc);
		qb_loop_timer_del(mainloop, ssh_op->assembly->keepalive_timer);
		qb_loop_job_del(mainloop, QB_LOOP_LOW, ssh_op,
			assembly_ssh_exec);
		assembly_state_changed(ssh_op->assembly, INSTANCE_STATE_FAILED);
		//free(ssh_op);
		return;
	}

	/*
	 * Add a healthcheck if asssembly is still running
	 */
	if (ssh_op->assembly->instance_state == INSTANCE_STATE_RUNNING) {
		qb_log(LOG_NOTICE, "adding a healthcheck timer for assembly '%s'", ssh_op->assembly->name);
		qb_loop_timer_add(mainloop, QB_LOOP_HIGH,
			HEALTHCHECK_TIMEOUT * QB_TIME_NS_IN_MSEC, ssh_op->assembly,
			assembly_healthcheck, &ssh_op->assembly->healthcheck_timer);
	}
}

static void assembly_healthcheck(void *data)
{
	struct assembly *assembly = (struct assembly *)data;

	ssh_nonblocking_exec(assembly, NULL, NULL,
		assembly_healthcheck_completion,
		"uptime");
}


static void ssh_keepalive_send(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	int seconds_to_next;
	int rc;

	rc = libssh2_keepalive_send(assembly->session, &seconds_to_next);
	qb_loop_timer_add(NULL, QB_LOOP_LOW, seconds_to_next * 1000 * QB_TIME_NS_IN_MSEC,
		assembly, ssh_keepalive_send, &assembly->keepalive_timer);
}
static void ssh_assembly_connect(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	char name[1024];
	char name_pub[1024];
	int i;
	int rc;

	switch (assembly->ssh_state) {
	case SSH_SESSION_INIT:
		assembly->session = libssh2_session_init();
		if (assembly->session == NULL) {
			rc = libssh2_session_last_errno(assembly->session);
			if (rc == LIBSSH2_ERROR_EAGAIN) {
				goto job_repeat_schedule;
			}
			qb_log(LOG_NOTICE,
				"session init failed %d\n", rc);
		}

		libssh2_session_set_blocking(assembly->session, 0);
		assembly->ssh_state = SSH_SESSION_STARTUP;

		/*
                 * no break here is intentional
		 */

	case SSH_SESSION_STARTUP:
		rc = libssh2_session_startup(assembly->session, assembly->fd);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"session startup failed %d\n", rc);
			goto error;
		}

		/*
                 * no break here is intentional
		 */
		assembly->ssh_state = SSH_KEEPALIVE_CONFIG;

	case SSH_KEEPALIVE_CONFIG:
		libssh2_keepalive_config(assembly->session, 1, KEEPALIVE_TIMEOUT);
		qb_loop_job_add(mainloop, QB_LOOP_LOW, assembly, ssh_keepalive_send);

		/*
                 * no break here is intentional
		 */
		assembly->ssh_state = SSH_USERAUTH_PUBLICKEY_FROMFILE;

	case SSH_USERAUTH_PUBLICKEY_FROMFILE:
		sprintf (name, "/var/lib/pacemaker-cloud/keys/%s",
			assembly->name);
		sprintf (name_pub, "/var/lib/pacemaker-cloud/keys/%s.pub",
			assembly->name);
		rc = libssh2_userauth_publickey_fromfile(assembly->session,
			"root", name_pub, name, "");
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc) {
			qb_log(LOG_ERR,
				"Authentication by public key for '%s' failed\n",
				assembly->name);
		} else {
			qb_log(LOG_NOTICE,
				"Authentication by public key for '%s' successful\n",
				assembly->name);
		}
		assembly_healthcheck(assembly);

		assembly_state_changed(assembly, INSTANCE_STATE_RUNNING);
		assembly->ssh_state = SSH_CONNECTED;
	}
error:
	return;

job_repeat_schedule:
	qb_loop_job_add(mainloop, QB_LOOP_LOW, assembly, ssh_assembly_connect);
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
		qb_loop_job_add(mainloop, QB_LOOP_LOW, assembly, ssh_assembly_connect);
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
	assembly->sin.sin_family = AF_INET;
	assembly->sin.sin_port = htons(22);
	assembly->sin.sin_addr.s_addr = hostaddr;
	assembly->ssh_state = SSH_SESSION_INIT;

	qb_log(LOG_NOTICE, "Connection in progress to assembly '%s'",
		assembly->name);

	qb_loop_job_add(mainloop, QB_LOOP_LOW, assembly, connect_execute);
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
			PENDING_TIMEOUT * QB_TIME_NS_IN_MSEC, assembly,
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
	char *type;
	char *class;
	char resource_name[4096];

	resource = calloc(1, sizeof (struct resource));
	name = xmlGetProp(cur_node, "name");
	sprintf(resource_name, "rsc_%s_%s", assembly->name, name);
	resource->name = strdup(resource_name);
	type = xmlGetProp(cur_node, "type");
	resource->type = strdup(type);
	class = xmlGetProp(cur_node, "class");
	resource->class = strdup(class);
	resource->assembly = assembly;
	qb_map_put(assembly->resource_map, resource->name, resource);
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
	assembly->instance_state = INSTANCE_STATE_OFFLINE;
	assembly->resource_map = qb_skiplist_create();
	qb_list_init(&assembly->ssh_op_head);
	instance_create(assembly);
	qb_map_put(assembly_map, name, assembly);

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

void reload(void)
{
	xmlNode *cur_node;
	xmlNode *dep_node;
	uuid_t uuid_temp_id;
        xsltStylesheetPtr ss;
	const char *params[1];

	uuid_generate(uuid_temp_id);
	uuid_unparse(uuid_temp_id, crmd_uuid);

	if (_config != NULL) {
		xmlFreeDoc(_config);
		_config = NULL;
	}
	if (_pe != NULL) {
		xmlFreeDoc(_pe);
		_pe = NULL;
	}

	_config = xmlParseFile("/var/run/dep-wp.xml");

	ss = xsltParseStylesheetFile(BAD_CAST "/usr/share/pacemaker-cloud/cf2pe.xsl");
	params[0] = NULL;

        _pe = xsltApplyStylesheet(ss, _config, params);
        xsltFreeStylesheet(ss);
        dep_node = xmlDocGetRootElement(_config);


        for (cur_node = dep_node->children; cur_node;
             cur_node = cur_node->next) {
                if (cur_node->type == XML_ELEMENT_NODE) {
                        if (strcmp((char*)cur_node->name, "assemblies") == 0) {
				assemblies_create(cur_node->children);
                        }
                }
        }
}


int main (void)
{
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

	assembly_map = qb_skiplist_create();
	op_history_map = qb_skiplist_create();

	reload();

	qb_loop_run(mainloop);
	return 0;
}
