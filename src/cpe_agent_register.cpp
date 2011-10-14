#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>

#include "cpe_agent.h"

typedef struct curl_recv_info {
	char *data;
	size_t size;
} curl_recv_info_t;

static size_t
curl_recv(void *ptr, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	curl_recv_info_t *mem = (curl_recv_info_t *)userp;

	mem->data = (char *)realloc(mem->data, mem->size + realsize + 1);
	if (!mem->data) {
		fprintf(stderr, "couldn't allocate curl recv buffer\n");
		exit(EXIT_FAILURE);
	}

	memcpy(&(mem->data[mem->size]), ptr, realsize);
	mem->size += realsize;
	mem->data[mem->size] = 0;

	return realsize;
}

static size_t
curl_process_header(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	char *ptr = (char *)contents;
	char **location = (char **)userp;

	if (strncmp((char *)(ptr), "Location:", 9) == 0) {
		*location = strndup(ptr+10, size-10);
		if (!*location) {
			fprintf(stderr, "couldn't allocate Location: buffer\n");
			exit(EXIT_FAILURE);
		}
	}

	return realsize;
}

/* Register our http server with conductor,
 * and record the correspond hook returned by conductor.
 *
 * TODO: This is a synchronous function, and requires
 * conductor to be already started.  Support retries
 * to remove the ordering dependency.  */

bool CpeAgent::register_hook(void)
{
	bool status = false;

	CURL* curl = curl_easy_init();
	if (!curl)
		return false;

	/* TODO: change localhost below to the actual hostname,
	* if conductor host is not localhost.  */
	char *hook_location = NULL;
	asprintf(&hook_location,
		 "<hook>"
		 "  <uri>https://%s:%d/pacemaker-cloud/api</uri>"
		 "</hook>",
		 "localhost", this->http_port());
	if (!hook_location) {
		fprintf(stderr, "Couldn't allocate hook_location\n");
		return false;
	}

	curl_recv_info_t response = {0,};
	char *conductor_location = NULL;

	struct curl_slist *headerlist = NULL;
	char *conductor_url;
	asprintf(&conductor_url, "http://%s:%d/api/hooks",
		 this->conductor_host(), this->conductor_port());
	if (!conductor_url) {
		fprintf(stderr, "Couldn't allocate conductor_url\n");
		free(hook_location);
		return false;
	}

	curl_easy_setopt(curl, CURLOPT_URL, conductor_url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, hook_location);
	headerlist = curl_slist_append(headerlist, "Content-Type: application/xml");
	headerlist = curl_slist_append(headerlist, "Accept: application/xml");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "cpe-agent/1.0");

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_recv);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_process_header);
	curl_easy_setopt(curl, CURLOPT_WRITEHEADER, &conductor_location);

	CURLcode res = curl_easy_perform(curl);

	if (res) {
		fprintf(stderr, "Error registering hook with conductor: %s\n", curl_easy_strerror(res));
	} else {
		if (!conductor_location) {
			fprintf(stderr, "Error registering hook with conductor: no Location: header\n");
			fprintf(stderr, "%s\n", response.data);
		} else {
			status = true;
			this->conductor_hook = conductor_location;
			/* We currently ignore the confirmation xml in the body (response.data)
                         * and just rely on the location.  */
			fprintf(stderr, "HOOK [info] %s\n", this->conductor_hook.c_str());
		}
	}

	free(conductor_location);
	free(response.data);
	curl_easy_cleanup(curl);
	free(conductor_url);
	curl_slist_free_all(headerlist);
	free(hook_location);

	return status;
}
