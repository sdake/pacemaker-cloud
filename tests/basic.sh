#!/bin/sh

set +e

distro=$1
arch=$2
nodes="1 2"
templ=t_$distro\_$arch\_1
dep=d_$distro\_$arch

declare -A ip
declare -A mac

if [ -f $templ.tdl ]
then
	cp -f $templ.tdl /var/lib/pacemaker-cloud/assemblies/
else
	echo
	echo "  Error need $templ.tdl"
	echo
	exit 1
fi

function deployable_create() {

	pcloudsh jeos_list | grep $distro
	if [ $? -ne 0 ]
	then
		pcloudsh jeos_create $distro $arch
	else
		echo "jeos $distro already created."
	fi

	pcloudsh deployable_list | grep $dep
	if [ $? -ne 0 ]
	then
		pcloudsh deployable_create $dep
	else
		echo "deployable $dep already created."
	fi

	pcloudsh assembly_list > /tmp/ass_list
	for n in $nodes
	do
		name=t_$distro\_$arch\_$n
		grep $name /tmp/ass_list
		if [ $? -ne 0 ]
		then
			if [ "$name" = "$templ" ]
			then
				echo "Creating assembly $name..."
				pcloudsh assembly_create $name $distro $arch
			else
				echo "Cloning assembly $name from $templ..."
				pcloudsh assembly_clone $templ $name
			fi
		else
			echo "assembly $name already created."
		fi
		pcloudsh deployable_assembly_add $dep $name
		pcloudsh assembly_resource_add rcs_$n httpd $name
	done
}

# start the deployable
function deployable_start() {
	pcloudsh deployable_start $dep
}

# save the ip address
function discover_nodes() {
	all_done="no"
	while [ "$all_done" == "no" ]
	do
		echo "trying again..."
		all_done="yes"
		sleep 1

		for n in $nodes
		do
			name=t_$distro\_$arch\_$n

			if [ -z "${mac[$name]}" ]
			then
				mac[$name]=$(grep "mac address" /var/lib/pacemaker-cloud/assemblies/$name.xml | cut -d\" -f2)
			fi
			if [ -z "${ip[$name]}" ]
			then
				ip[$name]=$(grep ${mac[$name]} /proc/net/arp | cut -d' ' -f1)
				if [ -n "${ip[$name]}" ]
				then
					echo "ip for $name : ${mac[$name]} is ${ip[$name]}"
				fi
			fi
			if [ -z "${ip[$name]}" ]
			then
				all_done="no"
			fi
		done
	done
}

function kill_node() {
	local name=$1
	virsh destroy $name
	mac[$name]=
	ip[$name]=
}

function node_cmd() {
	ssh -o StrictHostKeyChecking=no ${ip[$1]} $2
	return $?
}

function nodes_are_up() {

	local timeout=$1
	local slept=0
	local started="no"

	while [ "$started" == "no" ]
	do
		local all_started="yes"
		for n in $nodes
		do
			name=t_$distro\_$arch\_$n
			virsh list | grep $name > /dev/null
			if [ $? -ne 0 ]
			then
				echo "$name not started"
				all_started="no"
			else
				virsh dominfo $name | grep "State:.*running" > /dev/null
				if [ $? -ne 0 ]
				then
					echo "$name not started"
					all_started="no"
				fi
			fi
		done
		started=$all_started
		if [ "$started" == "no" ]
		then
			sleep 10
			let "slept=$slept + 10"
			echo "DEBUG: nodes are NOT all running $slept/$timeout"
		else
			echo "DEBUG: nodes are running $slept/$timeout"
			return 0
		fi
		if [ $slept -gt $timeout ]
		then
			return 1
		fi
	done
	return 0
}

# confirm resources are actually running
function resources_are_up() {
	local timeout=$1
	local slept=0
	local started="no"

	while [ "$started" == "no" ]
	do
		all_started="yes"
		for n in $nodes
		do
			name=t_$distro\_$arch\_$n
			if [ "$distro" == "F15" ]
			then
				MSG=$(node_cmd $name "service httpd status")
				echo $MSG | grep "Active: active (running)" > /dev/null
			else
				node_cmd $name "service httpd status" > /dev/null
			fi
			if [ $? -ne 0 ]
			then
				all_started="no"
			fi
		done
		started=$all_started
		if [ "$started" == "no" ]
		then
			sleep 10
			let "slept=$slept + 10"
			echo "DEBUG: resources NOT all running $slept/$timeout"
		else
			echo "DEBUG: resources are running $slept/$timeout"
			return 0
		fi
		if [ $slept -gt $timeout ]
		then
			return 1
		fi
	done
	return 0
}

function msg_start() {
	echo "  ===================================="
	echo "  TEST: $@"
	echo
}

function msg_result() {
	echo
	if [ $1 -eq 0 ]
	then
		echo "  PASSED: $@"
	else
		echo "  FAILED: $@"
	fi
	echo
}

function test_nodes_up() {
	local tn="all nodes started"

	msg_start $tn
	nodes_are_up 360
	msg_result $? $tn
}

function test_resources_up() {
	local tn="resources have started"

	msg_start $tn
	resources_are_up 360
	msg_result $? $tn
}

# kill one of the resources
# check that it gets restarted
function test_resource_restart() {
	local tn="recover from a failed resource ($1)"
	local name=t_$distro\_$arch\_$1

	msg_start $tn
	node_cmd $name "service httpd stop"

	resources_are_up 360
	msg_result $? $tn
}

# shutdown a VM
# check that the VM & resource get restarted
function test_node_restart() {
	local tn="recover from a failed node"
	local name=t_$distro\_$arch\_1

	msg_start $tn
	kill_node $name
	sleep 10
	discover_nodes

	nodes_are_up 360
	RC=$?
	if [ $RC -ne 0 ]
	then
		msg_result $RC "To restart the VM"
		return
	fi

	resources_are_up 120
	msg_result $? $tn
}

function deployable_stop() {
	pcloudsh deployable_stop $dep
}

function deployable_delete() {
	for n in $nodes
	do
		name=t_$distro\_$arch\_$n

		pcloudsh assembly_resource_remove rcs_$n $name
		pcloudsh deployable_assembly_remove $dep $name
		pcloudsh assembly_delete $name
		rm -f /var/lib/libvirt/images/$name.qcow2

		#TODO this probably needs to be in assembly_delete
		rm -f /var/lib/pacemaker-cloud/assemblies/$name.xml
	done

	# TODO need this command
	#pcloudsh deployable_delete $dep
}

deployable_create
deployable_start
discover_nodes
sleep 5
test_nodes_up
test_resources_up
for f in 1 2 2 1 2
do
	test_resource_restart $f
done
test_node_restart
deployable_stop
deployable_delete

