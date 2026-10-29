// loom/net/host_guard.h — outbound request guard for multi-user servers (R18).
//
// In accounts mode every user can set provider/endpoint URLs; without a guard the server becomes an
// open proxy into its own network (cloud metadata 169.254.169.254, localhost admin ports, other
// users' services). check_public_url refuses a URL whose host is, or resolves to, a loopback, private,
// link-local, unique-local, multicast or unspecified address, unless the host is in `allow_hosts`
// (exact host names or IP literals, e.g. a local Ollama). The check resolves the name at call time;
// a name that changes its answer between the check and the connection (DNS rebinding) is outside what it
// defends against.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"

namespace loom::net {

bool is_non_public_address(std::string_view ip_literal);
Status check_public_url(std::string_view url, const std::vector<std::string>& allow_hosts = {});
// Outbound allow-list (R103 B2, guest runtimes): a URL whose host is neither one of `hosts` nor a subdomain of one
// is refused. An empty list allows every host (the list is then not in use).
Status check_listed_host(std::string_view url, const std::vector<std::string>& hosts);

}  // namespace loom::net
