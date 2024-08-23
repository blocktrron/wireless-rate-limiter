#!/bin/sh

ACTION="$1"
ID="$2"
INTERFACE="$3"
MAC_ADDRESS="$4"
DOWNSPEED="$5"
UPSPEED="$6"
IFB_INTERFACE="$INTERFACE-ifb"

. /lib/wireless-rate-limiter/htb-shared.sh

function mac_filter_policy_add() {
	local iface
	local filter_id
	local direction
	local mac
	
	iface="$1"
	filter_id="$2"
	direction="$3"
	mac="$4"

	local flow_id
	local filter_handle

	flow_id="1:${filter_id}"
	filter_handle="800::${filter_id}"
	
	tc filter add dev "$iface" protocol all parent 1: prio 1 handle "$filter_handle" u32 match ether "$direction" "$mac" flowid "$flow_id"
}

function mac_filter_policy_remove() {
	local iface
	local filter_id
	
	iface="$1"
	filter_id="$2"

	local filter_handle
	filter_handle="800::${filter_id}"

	tc filter del dev "$iface" protocol all parent 1: prio 1 handle "$filter_handle" u32
}

function set_client_policy() {
	local id
	local iface
	local ifbdev
	local mac
	local rate_down
	local rate_up
	
	id="$1"
	iface="$2"
	ifbdev="$3"
	mac="$4"
	rate_down="$5"
	rate_up="$6"

	if [ -n "$rate_down" ]; then
		qdisc_add_child $iface $id "$rate_down"
		mac_filter_policy_add $iface $id "dst" "$mac"
	fi

	if [ -n "$rate_up" ]; then
		qdisc_add_child $ifbdev $id "$rate_up"
		mac_filter_policy_add $ifbdev $id "src" "$mac"
	fi
}

function remove_client_policy() {
	local id
	local iface
	local ifbdev
	local mac
	
	id="$1"
	iface="$2"
	ifbdev="$3"
	mac="$4"
	
	mac_filter_policy_remove $iface $id
	qdisc_remove_child $iface $id
	mac_filter_policy_remove $ifbdev $id
	qdisc_remove_child $ifbdev $id
}

if [ "$ACTION" = "add" ]; then
	remove_client_policy "$ID" "$INTERFACE" "$IFB_INTERFACE" "$MAC_ADDRESS"
	set_client_policy "$ID" "$INTERFACE" "$IFB_INTERFACE" "$MAC_ADDRESS" "$DOWNSPEED" "$UPSPEED"
elif [ "$ACTION" = "remove" ]; then
	remove_client_policy "$ID" "$INTERFACE" "$IFB_INTERFACE" "$MAC_ADDRESS"
fi