#pragma once
#include <stdint.h>
#include <mdns.h>

// The primary mDNS name belongs to the device for its entire uptime.
// Fleet leadership only adds/removes an alias; open browser tabs keep working.
class GotekFleetAlias {
  bool advertised = false;
  uint32_t address = 0;
public:
  bool setLeader(bool leader, const char* alias, uint32_t ip) {
    if (!leader) {
      if (advertised && mdns_delegate_hostname_remove(alias) != ESP_OK)
        return false;
      advertised = false;
      return true;
    }
    if (advertised && address == ip) return true;
    mdns_ip_addr_t addr = {};
    addr.addr.type = ESP_IPADDR_TYPE_V4;
    addr.addr.u_addr.ip4.addr = ip;
    esp_err_t err = advertised
      ? mdns_delegate_hostname_set_address(alias, &addr)
      : mdns_delegate_hostname_add(alias, &addr);
    if (err != ESP_OK) return false;
    advertised = true;
    address = ip;
    return true;
  }
};
