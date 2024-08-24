function qdisc_add_child() {
	local interface
	local id
	local ceil
	local htb_burst
	local fq_flows
	local fq_packets
	
	interface="$1"
	id="$2"
	ceil="$3"

	ceil=""
	if [ -n "$ceil" ]; then
		ceil="ceil $ceil"
	fi

	htb_burst=64k
	if [ "$id" -gt "9" ]; then
		htb_burst=16k
	fi

	fq_flows=1024
	if [ "$id" -gt "9" ]; then
		fq_flows=64
	fi

	fq_packets=4096
	if [ "$id" -gt "9" ]; then
		fq_packets=1024
	fi

	tc class replace dev "$interface" parent 1:1 classid "1:$id" htb rate 1mbit $ceil burst "$htb_burst" prio 1 quantum 4096
	tc qdisc replace dev "$interface" parent "1:$id" handle "$id:" fq_codel flows "$fq_flows" limit "$fq_packets" noecn
}

function qdisc_remove_child() {
	local interface
	local id
	
	interface="$1"
	id="$2"
	
	tc class del dev "$interface" parent 1:1 classid "1:$id"
	tc qdisc del dev "$interface" parent "1:$id" handle "$id:"
}
