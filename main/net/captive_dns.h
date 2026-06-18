#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Tiny UDP:53 DNS responder that answers every A query with the SoftAP gateway
// (192.168.4.1), so a phone joining the setup AP is funnelled to the config
// portal. Start ONLY while the SoftAP is up — never in STA mode, or it would
// hijack the device's own DNS. Returns 0/-1.
int  captive_dns_start(void);
int  captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
