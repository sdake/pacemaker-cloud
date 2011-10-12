#!/usr/bin/env python
#
# Copyright (C) 2011 Red Hat, Inc.
#
# Author: Angus Salkeld <asalkeld@redhat.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#
import os
import time
import random
import unittest
import logging
import subprocess

class SimpleSetup(object):
    def __init__(self, distro):
        self.distro = distro
        self.arch = 'x86_64'
        self.templ = 't_%s_%s_1' % (self.distro, self.arch)
        self.name = 'd_%s_%s' % (self.distro, self.arch)
        self.assemblies = []
        self.mac = {}
        self.ip = {}
        self.l = logging.getLogger()

        if not os.access('%s.tdl' % self.templ, os.R_OK):
            raise Exception('missing template %s.tdl' % self.templ)
        subprocess.call(['cp', '-f', '%s.tdl' % self.templ, '/var/lib/pacemaker-cloud/assemblies/'])

        for a in range(1, 3):
            n = 't_%s_%s_%d' % (self.distro, self.arch, a)
            self.assemblies.append(n)
            self.mac[n] = None
            self.ip[n] = None

        jl = subprocess.check_output(["pcloudsh", 'jeos_list'])
        if self.distro in jl:
            self.l.debug('jeos %s already created' % (self.distro))
        else:
            subprocess.call(['pcloudsh', 'jeos_create', self.distro, self.arch])

        dl = subprocess.check_output(["pcloudsh", 'deployable_list'])
        if self.name in dl:
            self.l.debug('deployable %s already created' % (self.name))
        else:
            subprocess.call(['pcloudsh', 'deployable_create', self.name])

        al = subprocess.check_output(["pcloudsh", 'assembly_list'])
        for a in self.assemblies:
            if not a in al:
                if a == self.templ:
                    self.l.debug("Creating assembly %s..." % (a))
                    subprocess.call(['pcloudsh', 'assembly_create', a, self.distro, self.arch])
                else:
                    self.l.debug("Cloning assembly %s from %s..." % (a, self.templ))
                    subprocess.call(['pcloudsh', 'assembly_clone', self.templ, a])
            else:
                self.l.debug("assembly %s already created." % (a))
            subprocess.call(['pcloudsh', 'deployable_assembly_add', self.name, a])
            subprocess.call(['pcloudsh', 'assembly_resource_add', 'rcs_%s' % a, 'httpd', a])

    def delete(self):
        for a in self.assemblies:
            subprocess.call(['pcloudsh', 'assembly_resource_remove', 'rcs_%s' % a, a])
            subprocess.call(['pcloudsh', 'deployable_assembly_remove', self.name, a])
            subprocess.call(['pcloudsh', 'assembly_delete', a])
            subprocess.call(['rm', '-rf', '/var/lib/libvirt/images/%s.qcow2' % a])
            #TODO this probably needs to be in assembly_delete
            subprocess.call(['rm', '-f', '/var/lib/pacemaker-cloud/assemblies/%s.xml' % a])

        # TODO need this command
        #subprocess.call(['pcloudsh', 'deployable_delete', self.name])

    def victim_get(self):
        return self.assemblies[0]

    def start(self):
        subprocess.call(['pcloudsh', 'deployable_start', self.name])
        self.discover_addresses()

    def stop(self):
        subprocess.call(['pcloudsh', 'deployable_stop', self.name])

    def kill_node(self, name):
        self.l.info('killing assembly %s', name)
        subprocess.call(['virsh', 'destroy', name])
        self.mac[name] = None
        self.ip[name] = None
        time.sleep(10)
        self.discover_addresses()

    def addr_get(self, a):

        if self.mac[a] == None:
            fn = '/var/lib/pacemaker-cloud/assemblies/%s.xml' % (a)
            lines = open(fn, 'r').readlines()
            for line in lines:
                if 'mac address' in line:
                    self.mac[a] = line.split('"')[1]
                    break

        if self.mac[a] == None:
            self.l.debug('no mac for %s yet' % (a))
            return

        if self.ip[a] == None:
            lines = open('/proc/net/arp', 'r').readlines()
            for line in lines:
                if self.mac[a] in line:
                    self.ip[a] = line.split(' ')[0]
                    break
            if self.ip[a] != None:
                self.l.debug("ip for %s : %s is %s" % (a, self.mac[a], self.ip[a]))
            else:
                self.l.debug('no ip for %s yet' % (self.mac[a]))

    def discover_addresses(self):
        all_done = False

        while not all_done:
            all_done = True
            time.sleep(1)

            for a in self.assemblies:
                self.addr_get(a)

                if self.ip[a] == None:
                    all_done = False

    def rsh(self, node, cmd):
        p = subprocess.Popen(["ssh", "-o", 'StrictHostKeyChecking=no',
                             self.ip[node], cmd],
                             stderr=subprocess.PIPE, stdout=subprocess.PIPE, close_fds=True)
        (stdoutdata, stderrdata) = p.communicate()
        #print '> ssh %s %s' % (node, cmd)
        #print '< %d: %s' %(p.returncode, stdoutdata)
        return (p.returncode, stdoutdata, stderrdata)


    def wait_for_all_nodes_to_be_up(self, timeout):
        slept = 0
        started = False

        while not started:
            all_started = True
            for a in self.assemblies:
                vl = subprocess.check_output(['virsh', 'list'])
                if not a in vl:
                    self.l.debug("%s not started" % a)
                    all_started = False
                else:
                    di = subprocess.check_output(['virsh', 'dominfo', a])
                    if not 'running' in di:
                        self.l.debug("%s not started" % a)
                        all_started = False

            started = all_started
            if not started:
                time.sleep(10)
                slept = slept + 10
                self.l.info("nodes are NOT all running %d/%d" % (slept, timeout))
            else:
                self.l.debug("nodes are all running %d/%d" % (slept, timeout))
                return 0
            if slept >= timeout:
                return 1

        return 1

    def wait_for_all_resources_to_be_up(self, timeout):
        slept = 0
        started = False
        all_started = False
        rc = 1

        while not started:
            all_started = False
            for a in self.assemblies:
                rc = 7
                if self.distro == 'F15':
                    rc = 3
                    (rc, o, e) = self.rsh(a, "service httpd status")
                    for line in o.split('\n'):
                        if 'Active: active (running)' in line:
                            rc = 0
                else:
                    (rc, o, e) = self.rsh(a, "service httpd status")
                if rc == 0:
                    all_started = True

            started = all_started
            if not started:
                time.sleep(10)
                slept = slept + 10
                self.l.debug('resources NOT all running (%d) %d/%d' % (rc, slept, timeout))
            else:
                self.l.debug('resources all running %d/%d' % (slept, timeout))
                return 0
            if slept >= timeout:
                return 1
        return 1

class TestSimple(unittest.TestCase):

    def test01_nodes_all_up(self):

        rc = self.setup.wait_for_all_nodes_to_be_up(360)
        self.assertEqual(rc, 0)

    def test02_resources_all_up(self):

        rc = self.setup.wait_for_all_resources_to_be_up(360)
        self.assertEqual(rc, 0)

    def test_assembly_restart(self):

        victim = self.setup.victim_get()
        rc = self.setup.wait_for_all_nodes_to_be_up(360)
        self.assertEqual(rc, 0)

        self.setup.kill_node(victim)
        rc = self.setup.wait_for_all_nodes_to_be_up(360)
        self.assertEqual(rc, 0)

    def test_resources_restart(self):

        victim = self.setup.victim_get()
        rc = self.setup.wait_for_all_resources_to_be_up(360)
        self.assertEqual(rc, 0)

        self.setup.rsh(victim, 'service httpd stop')

        rc = self.setup.wait_for_all_resources_to_be_up(360)
        self.assertEqual(rc, 0)

class TestSimpleF15(TestSimple):

    def setUp(self):
        self.setup = simple_f15

class TestSimpleF14(TestSimple):

    def setUp(self):
        self.setup = simple_f14

if __name__ == '__main__':

    subprocess.call(['systemctl', '--system', 'daemon-reload'])
    subprocess.call(['systemctl', 'start', 'pcloud-cped.service'])

    logging.basicConfig(level=logging.INFO, format="F14: %(levelname)s %(funcName)s %(message)s")
    simple_f14 = SimpleSetup('F14')
    simple_f14.start()
    time.sleep(2)
    suite = unittest.TestLoader().loadTestsFromTestCase(TestSimpleF14)
    unittest.TextTestRunner(verbosity=2).run(suite)
    simple_f14.stop()
    simple_f14.delete()

    logging.basicConfig(level=logging.INFO, format="F15: %(levelname)s %(funcName)s %(message)s")
    simple_f15 = SimpleSetup('F15')
    simple_f15.start()
    time.sleep(2)
    suite = unittest.TestLoader().loadTestsFromTestCase(TestSimpleF15)
    unittest.TextTestRunner(verbosity=2).run(suite)
    simple_f15.stop()
    simple_f15.delete()

    time.sleep(1)
    subprocess.call(['systemctl', 'stop', 'pcloud-cped.service'])
    subprocess.call(['systemctl', 'stop', 'pcloud-vmlauncher.service'])
    subprocess.call(['systemctl', 'stop', 'pcloud-qpidd.service'])

