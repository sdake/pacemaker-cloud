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
import time
import random
import unittest
import logging
import manufacturer
import deployable
from process_monitor import ProcessMonitor

class TestAeolusHA(unittest.TestCase):

    def setUp(self):
        self.qpidd = ProcessMonitor(['qpidd', '-p', '49000', '--auth', 'no'])
        time.sleep(1)
        self.cped = ProcessMonitor(['../src/cped', '-v', '-v', '-v'])
        self.manufacturer = manufacturer.Manufacturer()
        time.sleep(2)

    def test_one_assembly(self):
        '''
        Start a deployable wth one assembly.
        Assertion: it is started and we can run acommand on it.
        '''
        self.assertTrue(self.qpidd.is_running())
        self.assertTrue(self.cped.is_running())

        d = deployable.Deployable('test')
        ai1 = self.manufacturer.assemble('f14-cpe-test', 1)
        d.assembly_add(ai1)
        d.start()
        (rc, out) = d.assemblies['f14-cpe-test-1'].rsh('hostname')
        self.assertEqual(rc, 0)
        self.assertEqual(out.strip(), 'f14-cpe-test-1')
        d.stop()

        self.assertTrue(self.qpidd.is_running())
        self.assertTrue(self.cped.is_running())

    def tearDown(self):
        #self.manufacturer.stop()
        self.cped.stop()
        self.qpidd.stop()
        pass

if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG, format="%(message)s")
    suite = unittest.TestLoader().loadTestsFromTestCase(TestAeolusHA)
    unittest.TextTestRunner(verbosity=2).run(suite)


