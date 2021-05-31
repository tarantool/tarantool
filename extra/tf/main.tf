resource "openstack_compute_instance_v2" "instance" {
  count = var.instance_count

  name = "tarantool_testing-${count.index + 1}-${var.id}"

  image_name = "Ubuntu-18.04-202003"

  image_id = "3f03b393-d45b-455d-bdf5-0ac4399ecf42"

  flavor_name = "Basic-1-2-20"

  key_pair = var.keypair_name

  config_drive = true

  availability_zone = "DP1"

  security_groups = [
    "default",
    "allow-ssh",
    "allow-tarantool"
  ]

  network {
    name = "ext-net"
  }

  provisioner "remote-exec" {
    inline = [
      "set -o errexit",
      "sudo hostnamectl set-hostname n${count.index + 1}",
      "sudo apt-get -o Debug::Acquire::http=true -o Debug::pkgAcquire::Worker=1 update"
    ]
  }

  connection {
    host = self.access_ip_v4
    type = "ssh"
    user = "ubuntu"
    timeout = "5m"
    agent = true
    private_key = file(var.ssh_key_path)
  }
}
