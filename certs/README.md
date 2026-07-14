# CA Certificate Bundle

`cacert.pem` is the Mozilla CA certificate bundle, downloaded from https://curl.se/ca/cacert.pem.

It is used by the C++ HTTP client (libcurl) to verify SSL certificates when making outgoing HTTPS requests (e.g., to the Square API).

## Updating

To update the bundle, download the latest version:

```bash
curl -o cacert.pem https://curl.se/ca/cacert.pem
```

Mozilla updates this bundle periodically as certificate authorities are added or removed.
