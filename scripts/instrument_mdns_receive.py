#!/usr/bin/env python3
"""Opt-in local diagnostic instrumentation. Reinstall managed dependency to remove."""
import pathlib
root=pathlib.Path('idf_app/managed_components/espressif__mdns')
patches={
'mdns_networking_lwip.c':[
('    uint8_t i;\n    while (pb != NULL)', '    static unsigned trace_count;\n    bool trace = trace_count++ < 256;\n    if (trace) ESP_LOGI(TAG, "RX src=%s port=%u bytes=%u", ipaddr_ntoa(raddr), (unsigned)rport, pb ? (unsigned)pb->tot_len : 0);\n    uint8_t i;\n    while (pb != NULL)'),
('                        //packet source is not in the same subnet', '                        if (trace) ESP_LOGW(TAG, "RX drop: source outside interface subnet");\n                        //packet source is not in the same subnet'),
('        if (!found || send_rx_action(packet) != ESP_OK) {', '        esp_err_t queued = found ? send_rx_action(packet) : ESP_ERR_INVALID_STATE;\n        if (trace) ESP_LOGI(TAG, "RX interface=%u found=%d queue=%s", (unsigned)packet->tcpip_if, found, esp_err_to_name(queued));\n        if (!found || queued != ESP_OK) {')],
'mdns_receive.c':[
('    DBG_RX_PACKET(packet, data, len);','    static unsigned trace_packets;\n    bool trace = trace_packets++ < 256;\n    if (trace) ESP_LOGI(TAG, "PARSE bytes=%u interface=%u protocol=%d", (unsigned)len, (unsigned)packet->tcpip_if, (int)packet->ip_protocol);\n    DBG_RX_PACKET(packet, data, len);'),
('                search_result = mdns_priv_query_find(name, type, packet->tcpip_if, packet->ip_protocol);','                search_result = mdns_priv_query_find(name, type, packet->tcpip_if, packet->ip_protocol);\n                if (trace) ESP_LOGI(TAG, "RECORD type=%u host=%s service=%s proto=%s search_match=%d", (unsigned)type, name->host, name->service, name->proto, search_result != NULL);')]
}
for name,replacements in patches.items():
 p=root/name;s=p.read_text()
 for old,new in replacements:
  if new in s:continue
  if s.count(old)!=1:raise SystemExit(f'Unexpected dependency source: {name}: {old[:40]}')
  s=s.replace(old,new)
 p.write_text(s)
print('Receive/parser tracing installed; first 256 packets per layer only')
