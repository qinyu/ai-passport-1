#include "codex_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", what);
        g_failed = 1;
    }
}

static codex_packet_t parse(const char *json)
{
    codex_packet_t packet;
    int rc = codex_protocol_parse(json, strlen(json), &packet);
    expect(rc == 0, json);
    return packet;
}

int main(void)
{
    codex_packet_t packet = parse(
        "{\"state\":\"busy\",\"tokens\":184502,\"primary\":42,\"secondary\":15,"
        "\"primary_resets_at\":1775731234,\"secondary_resets_at\":1776331234,"
        "\"primary_label\":\"5h\",\"secondary_label\":\"7d\",\"now\":1775700000,"
        "\"agent\":\"CODEX\"}");
    expect(packet.has_state && strcmp(packet.state, "busy") == 0, "state");
    expect(packet.tokens == 184502u, "tokens");
    expect(packet.primary == 42 && packet.secondary == 15, "percents");
    expect(packet.primary_resets_at == 1775731234u, "primary reset");
    expect(strcmp(packet.primary_label, "5h") == 0, "5h label");
    expect(strcmp(packet.secondary_label, "7d") == 0, "7d label");
    expect(packet.now_epoch == 1775700000u, "now");
    expect(!packet.has_prompt, "usage does not clear prompt");
    packet = parse("{\"agent\":\"PI\",\"via\":\"herdr\"}");
    expect(packet.has_agent && strcmp(packet.agent, "PI") == 0, "agent");
    expect(packet.has_via && strcmp(packet.via, "herdr") == 0, "via");

    packet = parse("{\"primary\":-4,\"secondary\":140}");
    expect(packet.primary == 0 && packet.secondary == 100, "clamp percent");

    packet = parse("{\"prompt\":{\"id\":\"h1\",\"tool\":\"Bash\",\"hint\":\"rm -rf /tmp\"}}");
    expect(packet.has_prompt && !packet.prompt_clear, "prompt present");
    expect(strcmp(packet.prompt_id, "h1") == 0, "prompt id");
    expect(strcmp(packet.prompt_tool, "Bash") == 0, "prompt tool");
    expect(strcmp(packet.prompt_hint, "rm -rf /tmp") == 0, "prompt hint");

    packet = parse("{\"prompt\":null}");
    expect(packet.has_prompt && packet.prompt_clear, "prompt clear");

    packet = parse("{\"time\":[1775731234,-25200]}");
    expect(packet.has_time && packet.time_epoch == 1775731234u, "time epoch");
    expect(packet.time_tz_offset == -25200, "time zone");

    packet = parse("{\"cmd\":\"status\"}");
    expect(packet.has_cmd && strcmp(packet.cmd, "status") == 0, "status cmd");

    packet = parse("{\"state\":\"idle\",\"hint\":\"\\u4e2d\"}");
    expect(strcmp(packet.state, "idle") == 0, "ignored unknown still parses");

    expect(codex_protocol_parse("{", 1, &packet) != 0, "truncated object");
    expect(codex_protocol_parse("[]", 2, &packet) != 0, "array rejected");
    expect(codex_protocol_parse("{\"state\":\"busy\"} trailing", 25, &packet) != 0, "trailing junk");

    {
        char json[128];
        expect(codex_protocol_permission_json(json, sizeof json, "h1", "accept") == 0, "perm build");
        expect(strcmp(json, "{\"cmd\":\"permission\",\"id\":\"h1\",\"decision\":\"accept\"}") == 0, "perm text");
        expect(codex_protocol_permission_json(json, sizeof json, "h1", "once") != 0, "reject once");
        expect(codex_protocol_ack_json(json, sizeof json, "owner", true) == 0, "ack");
        expect(strstr(json, "\"ack\":\"owner\"") != NULL && strstr(json, "\"ok\":true") != NULL, "ack body");
        expect(codex_protocol_status_json(json, sizeof json, "Codex-AB", true, 87, 4012, 2, 1) == 0, "status");
        expect(strstr(json, "\"sec\":true") != NULL, "secure");
        expect(strstr(json, "\"pct\":87") != NULL, "battery pct");
        expect(codex_protocol_status_json(json, sizeof json, "Codex-AB", false, -1, -1, 0, 0) == 0, "no bat");
        expect(strstr(json, "bat") == NULL, "omit battery");
    }

    {
        char hint_json[64];
        snprintf(hint_json, sizeof hint_json, "{\"prompt\":{\"id\":\"p\",\"tool\":\"T\",\"hint\":\"%s\"}}",
                 "\xe4\xb8\xad\xe6\x96\x87");
        packet = parse(hint_json);
        expect(strcmp(packet.prompt_hint, "\xe4\xb8\xad\xe6\x96\x87") == 0, "utf-8 hint");
    }

    if (g_failed) return 1;
    puts("test_codex_protocol: PASS");
    return 0;
}
