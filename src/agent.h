/* mh_agent.cpp - Copyright (C) 2010 Red Hat, Inc.
 * Written by Andrew Beekhof <andrew@beekhof.net>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __MATAHARI_DAEMON_H
#define __MATAHARI_DAEMON_H

#include <string>
#include <qpid/sys/Time.h>
#include <qpid/agent/ManagementAgent.h>
#include <qpid/management/Manageable.h>

extern "C" {
#include "mainloop.h"
}

using namespace qpid::management;
using namespace std;

#include "org/cloudpolicyengine/Package.h"
namespace _qmf = qmf::org::cloudpolicyengine;

class CpeAgent : public Manageable
{
    GMainLoop *mainloop;
    mainloop_fd_t *qpid_source;
    
  public:
    CpeAgent() {};
    ~CpeAgent() {};
    
    virtual int setup(ManagementAgent *agent) { return 0; };
    int init(int argc, char **argv, const char* proc_name);
    void run();
};

#endif // __MATAHARI_DAEMON_H
