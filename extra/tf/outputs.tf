output "instance_names" {
  description = "List of names assigned to the instances."
  value = openstack_compute_instance_v2.instance.*.name
}

output "instance_ips" {
  description = "List of public IP addresses assigned to the instances."
  value = openstack_compute_instance_v2.instance.*.access_ip_v4
  sensitive = true
}
