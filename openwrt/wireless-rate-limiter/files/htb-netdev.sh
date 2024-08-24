#!/bin/sh

. /lib/wireless-rate-limiter/htb-shared.sh

ACTION="$1"
INTERFACE="$2"

# How much all clients on the SSID is allowed to download
DOWNSPEED="$3"
# How much all clients on the SSID is allowed to upload
UPSPEED="$4"

IFB_INTERFACE="$INTERFACE-ifb"
IFB_PRIORITY=512

function qdisc_add() {
	local interface
	local speed
	
	interface="$1"
	speed="$2"

	if [ -z "$speed" ]; then
		speed="1000mbit"
	fi

	tc qdisc add dev "$interface" root handle 1: htb default 2
	tc class add dev "$interface" parent 1: classid 1:1 htb rate "$speed" burst 128k quantum 8192
	qdisc_add_child "$interface" 2 "$speed"
}

function qdisc_remove() {
	local interface
	local ifb_interface
	
	interface="$1"
	ifb_interface="$2"
	
	# Delete Queueing Discipline (From the interface)
	tc qdisc del dev "$interface" root

	# Delete Intermediate Functional Block
	# Implicitly deletes the Queueing Discipline (Towards the interface)
	tc filter del dev "$interface" ingress protocol all prio "$IFB_PRIORITY"
	ip link set "$ifb_interface" down
	ip link del "$ifb_interface"
}

if [ "$ACTION" = "add" ]; then
	# Delete existing configuration
	qdisc_remove "$INTERFACE" "$IFB_INTERFACE"

	# Create Intermediate Functional Block
	ip link add "$IFB_INTERFACE" type ifb
	ip link set "$IFB_INTERFACE" up

	# Redirect traffic to IFB
	tc qdisc add dev "$INTERFACE" clsact
	tc filter add dev "$INTERFACE" ingress protocol all prio "$IFB_PRIORITY" matchall action mirred egress redirect dev "$IFB_INTERFACE"

	# Create Queueing Discipline (Towards the interface)
	qdisc_add "$INTERFACE" "$DOWNSPEED"

	# Create Queueing Discipline (From the interface)
	qdisc_add "$IFB_INTERFACE" "$UPSPEED"
	exit 0
elif [ "$ACTION" = "remove" ]; then
	qdisc_remove "$INTERFACE" "$IFB_INTERFACE"
	exit 0
fi
