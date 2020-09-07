variable "user_name" {
  type = string
}

variable "password" {
  type = string
}

variable "tenant_id" {
  type = string
}

variable "user_domain_id" {
  type = string
}

variable "keypair_name" {
  type = string
}

variable "ssh_key_path" {
  type = string
}

variable "instance_count" {
  type = number
  default = 1
  description = "Number of instances."

  validation {
    condition = var.instance_count > 0
    error_message = "The instance_count value must be greater than zero."
  }
}

variable "id" {
  type = string
  default = null
}

provider "openstack" {
  user_name = var.user_name
  password = var.password
  tenant_id = var.tenant_id
  user_domain_id = var.user_domain_id
  auth_url = "https://infra.mail.ru/identity/v3/"
  region = "RegionOne"
  use_octavia = true
}
