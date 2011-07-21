#!/bin/sh

set +e

distro=$1
arch=$2
templ=t_$distro\_$arch\_1
dep=d_$distro\_$arch

if [ -f $templ.tdl ]
then
	cp $templ.tdl /var/lib/pacemaker-cloud/assemblies/
else
	echo
	echo "  Error need $templ.tdl"
	echo
	exit 1
fi

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
for n in 1 2
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

pcloudsh deployable_start $dep

# TODO
# 1) confirm resources are actually running
# 2) kill one of the resources
#    check that it gets restarted
# 3) shutdown a VM
#    check that the VM & resource get restarted
# 4) stop the deployable
pcloudsh deployable_stop $dep
#    confirm the VMs + depd are stopped

# cleanup
for n in 1 2
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

