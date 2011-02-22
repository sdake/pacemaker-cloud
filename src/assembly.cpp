/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Authors: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of cpe.
 *
 * cpe is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * cpe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cpe.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <map>
#include <assert.h>
#include "mainloop.h"
#include "assembly.h"

using namespace std;
using namespace qmf;


static gboolean host_proxy_timeout(gpointer data)
{
	Assembly *h = (Assembly *)data;
	ConsoleEvent event;
	bool got_event = false;

	if (!h->is_connected) {
		h->deref();
		return FALSE;
	}

	if (h->nextEvent(event)) {
		if (event.getType() == CONSOLE_EVENT) {
			const Data& data(event.getData(0));
			got_event = true;
			// TODO check the sequence number
			cout << "Event content " << data.getProperties() << endl;
		}
	}
	if (!got_event) {
		cout << "Aarg - no event in the last 5 secs!" << endl;
	}

	return TRUE;
}

bool Assembly::nextEvent(ConsoleEvent& e)
{
	return this->session->nextEvent(e);
}

void Assembly::deref(void)
{
	this->refcount--;
	if (this->refcount == 0) {
		delete this;
	}
}

void Assembly::stop(void)
{
	if (this->is_connected) {
		this->session->close();
		this->connection->close();
		this->is_connected = false;
	}
	this->deref();
}

Assembly::Assembly()
{
	this->is_connected = false;
	this->refcount = 1;
	this->session = NULL;
	this->connection = NULL;
	this->name = "";
}


Assembly::~Assembly()
{
	cout << "~Assembly() " << this->name << endl;
	if (this->is_connected) {
		this->session->close();
		this->connection->close();
	}
}

Assembly::Assembly(std::string& host_url)
{
	this->is_connected = false;
	this->refcount = 1;

	this->name = host_url;
	this->connection = new qpid::messaging::Connection(host_url, this->connectionOptions);;
	this->connection->open();

	this->session = new ConsoleSession(*this->connection, this->sessionOptions);
	this->session->open();

	this->is_connected = true;

	cout << "Assembly() " << this->name << endl;

	g_timeout_add(5000,
                      host_proxy_timeout,
		      this);
	this->refcount++;
}

static std::map<std::string, Assembly*> hosts;

int assembly_monitor_start(std::string& host_url)
{
	Assembly *h = hosts[host_url];
	if (h) {
		// don't want duplicates
		return -1;
	}

	try {
		h = new Assembly(host_url);
	} catch (qpid::types::Exception e) {
		cout << "Error: " << e.what() << endl;
		delete h;
		return -1;
	}
	hosts[host_url] = h;
	return 0;
}

int assembly_monitor_stop(string& host_url)
{
	Assembly *h = hosts[host_url];
	if (h) {
		h->stop();
		hosts.erase (host_url);
	}
	return 0;
}

