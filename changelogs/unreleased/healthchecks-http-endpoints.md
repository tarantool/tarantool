## feature/config

* Added optional Kubernetes-compatible `/healthz`, `/livez`, and `/readyz`
  endpoints to HTTP servers configured by `roles.httpd`. The endpoints can be
  enabled and customized for each server using its `health` option.
