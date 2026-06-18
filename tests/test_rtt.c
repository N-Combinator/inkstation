/*
 * test_rtt.c — host unit tests for rtt_parse().
 *
 * Pure C, no InkView / libcurl: compile and run on the build host with
 *   tests/run_host_tests.sh
 * exercising the JSON parser that backs the live board screen.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtt.h"

/* Stub out the network layer — these tests exercise rtt_parse() only. */
int http_get(const char *url, const char *bearer,
             char **out_buf, size_t *out_len, long *out_status,
             char *err, size_t errsz)
{
    (void)url; (void)bearer; (void)out_buf; (void)out_len;
    (void)out_status; (void)errsz;
    if (err) err[0] = '\0';
    return -1;
}

static int g_fail;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok   %s\n", msg); } \
    else { printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); g_fail++; } \
} while (0)

#define STREQ(a, b) ((a) && (b) && strcmp((a), (b)) == 0)

/* ---- fixtures ------------------------------------------------------ */

/* Minimal departures response: two services, one on time, one delayed. */
static const char DEP_JSON[] =
"{"
"  \"location\": { \"name\": \"Guildford\", \"crs\": \"GLD\" },"
"  \"services\": ["
"    { \"locationDetail\": {"
"        \"gbttBookedDeparture\": \"1010\","
"        \"realtimeDeparture\": \"1010\","
"        \"platform\": \"2\","
"        \"displayAs\": \"CALL\","
"        \"destination\": [ { \"description\": \"London Waterloo\" } ]"
"      },"
"      \"atocName\": \"South Western Railway\" },"
"    { \"locationDetail\": {"
"        \"gbttBookedDeparture\": \"1025\","
"        \"realtimeDeparture\": \"1031\","
"        \"platform\": \"\","
"        \"displayAs\": \"CALL\","
"        \"destination\": [ { \"description\": \"Weybridge\" } ]"
"      },"
"      \"atocName\": \"South Western Railway\" }"
"  ]"
"}";

/* Arrivals response: one service. */
static const char ARR_JSON[] =
"{"
"  \"location\": { \"name\": \"Guildford\", \"crs\": \"GLD\" },"
"  \"services\": ["
"    { \"locationDetail\": {"
"        \"gbttBookedArrival\": \"0942\","
"        \"realtimeArrival\": \"0942\","
"        \"platform\": \"3\","
"        \"displayAs\": \"CALL\","
"        \"origin\": [ { \"description\": \"London Waterloo\" } ]"
"      },"
"      \"atocName\": \"South Western Railway\" }"
"  ]"
"}";

/* Cancelled service. */
static const char CANCEL_JSON[] =
"{"
"  \"location\": { \"name\": \"Guildford\", \"crs\": \"GLD\" },"
"  \"services\": ["
"    { \"locationDetail\": {"
"        \"gbttBookedDeparture\": \"1100\","
"        \"realtimeDeparture\": \"1100\","
"        \"platform\": \"1\","
"        \"displayAs\": \"CANCELLED\","
"        \"destination\": [ { \"description\": \"London Waterloo\" } ],"
"        \"cancelReasonCode\": \"OA\""
"      },"
"      \"atocName\": \"South Western Railway\" }"
"  ]"
"}";

/* Service with no relevant leg (no gbttBookedDeparture) — should be skipped. */
static const char SKIP_JSON[] =
"{"
"  \"location\": { \"name\": \"Test\", \"crs\": \"TST\" },"
"  \"services\": ["
"    { \"locationDetail\": {"
"        \"displayAs\": \"ORIGIN\","
"        \"destination\": [ { \"description\": \"Somewhere\" } ]"
"      },"
"      \"atocName\": \"Test Operator\" }"
"  ]"
"}";

/* Empty services array. */
static const char EMPTY_JSON[] =
"{ \"location\": { \"name\": \"Guildford\", \"crs\": \"GLD\" },"
"  \"services\": [] }";

/* ---- test cases ----------------------------------------------------- */

static void test_departures(void)
{
    printf("\n[departures]\n");
    rtt_board b;
    char err[160] = {0};
    int rc = rtt_parse(DEP_JSON, RTT_DEPARTURES, &b, err, sizeof err);
    CHECK(rc == 0, "parse succeeds");
    CHECK(STREQ(b.name, "Guildford"), "station name");
    CHECK(STREQ(b.crs, "GLD"), "CRS");
    CHECK(b.count == 2, "two services");

    const rtt_service *s0 = &b.services[0];
    CHECK(STREQ(s0->time, "10:10"), "svc0 time formatted");
    CHECK(STREQ(s0->place, "London Waterloo"), "svc0 destination");
    CHECK(STREQ(s0->platform, "2"), "svc0 platform");
    CHECK(STREQ(s0->expected, "On time"), "svc0 on time");
    CHECK(s0->cancelled == 0, "svc0 not cancelled");
    CHECK(s0->delayed == 0, "svc0 not delayed");
    CHECK(STREQ(s0->operator_, "South Western Railway"), "svc0 operator");

    const rtt_service *s1 = &b.services[1];
    CHECK(STREQ(s1->time, "10:25"), "svc1 booked time");
    CHECK(STREQ(s1->expected, "10:31"), "svc1 realtime");
    CHECK(s1->delayed == 1, "svc1 delayed");
    CHECK(s1->cancelled == 0, "svc1 not cancelled");
    CHECK(STREQ(s1->platform, ""), "svc1 no platform");
}

static void test_arrivals(void)
{
    printf("\n[arrivals]\n");
    rtt_board b;
    char err[160] = {0};
    int rc = rtt_parse(ARR_JSON, RTT_ARRIVALS, &b, err, sizeof err);
    CHECK(rc == 0, "parse succeeds");
    CHECK(b.count == 1, "one service");
    CHECK(STREQ(b.services[0].time, "09:42"), "arrival time formatted");
    CHECK(STREQ(b.services[0].place, "London Waterloo"), "origin shown");
    CHECK(STREQ(b.services[0].platform, "3"), "platform");
    CHECK(STREQ(b.services[0].expected, "On time"), "on time");
}

static void test_cancelled(void)
{
    printf("\n[cancelled]\n");
    rtt_board b;
    char err[160] = {0};
    int rc = rtt_parse(CANCEL_JSON, RTT_DEPARTURES, &b, err, sizeof err);
    CHECK(rc == 0, "parse succeeds");
    CHECK(b.count == 1, "one service");
    CHECK(b.services[0].cancelled == 1, "marked cancelled");
    CHECK(STREQ(b.services[0].expected, "Cancelled"), "expected text");
}

static void test_skip_no_leg(void)
{
    printf("\n[skip service with no relevant leg]\n");
    rtt_board b;
    char err[160] = {0};
    /* Departures board — no gbttBookedDeparture, service must be skipped. */
    int rc = rtt_parse(SKIP_JSON, RTT_DEPARTURES, &b, err, sizeof err);
    CHECK(rc == 0, "parse succeeds");
    CHECK(b.count == 0, "no services (skip origin-only entry)");
}

static void test_empty(void)
{
    printf("\n[empty services]\n");
    rtt_board b;
    char err[160] = {0};
    int rc = rtt_parse(EMPTY_JSON, RTT_DEPARTURES, &b, err, sizeof err);
    CHECK(rc == 0, "parse succeeds");
    CHECK(b.count == 0, "count zero");
    CHECK(STREQ(b.name, "Guildford"), "station name");
}

static void test_bad_json(void)
{
    printf("\n[malformed JSON]\n");
    rtt_board b;
    char err[160] = {0};
    int rc = rtt_parse("{not json{{", RTT_DEPARTURES, &b, err, sizeof err);
    CHECK(rc == -1, "returns error");
    CHECK(err[0] != '\0', "error message set");
}

static void test_null_input(void)
{
    printf("\n[null input]\n");
    rtt_board b;
    char err[160] = {0};
    int rc = rtt_parse(NULL, RTT_DEPARTURES, &b, err, sizeof err);
    CHECK(rc == -1, "null returns error");
}

/* ---- main ---------------------------------------------------------- */

int main(void)
{
    printf("=== rtt_parse unit tests ===\n");
    test_departures();
    test_arrivals();
    test_cancelled();
    test_skip_no_leg();
    test_empty();
    test_bad_json();
    test_null_input();
    printf("\n%s  (%d failure%s)\n",
           g_fail ? "FAIL" : "PASS", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
