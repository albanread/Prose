# UserBootscript for network probing: injected into a run's image copy by
# run-vz.sh when PW_INJECT_SCRIPT points here. Results land in /boot/home/netprobe.txt,
# fetched with private_workspace/extract.sh <name> /home/netprobe.txt.
# (plain sh only: the minimum image has no grep/sed)
(
	sleep 30
	{
		echo "== $(date)"
		echo "== ifconfig (after 30 s of DHCP)"; ifconfig /dev/net/virtio/0
		case "$(ifconfig /dev/net/virtio/0)" in
			*"inet addr: --"*)
				echo "== no lease: static 192.168.64.10/24 via 192.168.64.1"
				ifconfig /dev/net/virtio/0 192.168.64.10 netmask 255.255.255.0 up
				route add /dev/net/virtio/0 default gw 192.168.64.1
				sleep 2
				;;
		esac
		echo "== route"; route
		echo "== ping gateway"; ping -c 3 192.168.64.1
		echo "== ping 1.1.1.1"; ping -c 3 1.1.1.1
		echo "== dns + ping"; ping -c 2 google.com
		echo "== big ping (1400 bytes)"; ping -c 2 -s 1400 1.1.1.1
		echo "== arp"; arp -a
		echo "== ifconfig"; ifconfig /dev/net/virtio/0
	} > /boot/home/netprobe.txt 2>&1
) &
