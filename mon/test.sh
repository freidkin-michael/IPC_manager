#!/bin/bash
#
# MON System Comprehensive Test Suite
# Tests for typed status pub-sub system
#

set -e

BUILD_DIR="../build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Counters
TESTS_PASSED=0
TESTS_FAILED=0

# Cleanup function
cleanup() {
    pkill -9 -f "publisher_" 2>/dev/null || true
    pkill -9 -f "subscriber_" 2>/dev/null || true
    pkill -9 -f "mon_status" 2>/dev/null || true
    rm -f /dev/shm/typed_status_shm 2>/dev/null || true
    sleep 0.3
}

trap cleanup EXIT

# Test result functions
pass() {
    echo -e "${GREEN}[+] PASS${NC}: $1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

fail() {
    echo -e "${RED}[-] FAIL${NC}: $1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}        MON System - Comprehensive Test Suite                       ${NC}"
echo -e "${CYAN}====================================================================${NC}"
echo ""

################################################################################
# Section 1: Basic Functionality
################################################################################
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}  Section 1: Basic Functionality & Initialization                   ${NC}"
echo -e "${CYAN}====================================================================${NC}"

info "Test 1: Clean slate - no stale shared memory"
cleanup
if [ ! -f /dev/shm/typed_status_shm ]; then
    pass "No stale shared memory"
else
    fail "Stale shared memory exists"
fi

info "Test 2: Publisher startup"
$BUILD_DIR/publisher_dac_prach > /dev/null 2>&1 &
PUB1_PID=$!
$BUILD_DIR/publisher_phy > /dev/null 2>&1 &
PUB2_PID=$!
sleep 2

if ps -p $PUB1_PID > /dev/null 2>&1 && ps -p $PUB2_PID > /dev/null 2>&1; then
    pass "Both publishers started (PIDs: $PUB1_PID, $PUB2_PID)"
else
    fail "Publishers failed to start"
    exit 1
fi

info "Test 3: Shared memory creation"
if [ -f /dev/shm/typed_status_shm ]; then
    pass "Shared memory created"
else
    fail "Shared memory not created"
fi

info "Test 4: Status registration (3 statuses expected)"
STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
STATUS_COUNT=$(echo "$STATUS_OUTPUT" | grep "Registered Statuses:" | awk '{print $3}')

if [ "$STATUS_COUNT" = "3" ]; then
    pass "All 3 statuses registered (dac, phy, prach)"
else
    fail "Expected 3 statuses, got $STATUS_COUNT"
fi

# Verify each status individually
for status in dac_status phy_status prach_status; do
    if echo "$STATUS_OUTPUT" | grep -q "^$status"; then
        pass "$status registered"
    else
        fail "$status not found"
    fi
done

################################################################################
# Section 2: Data Integrity
################################################################################
echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}  Section 2: Data Integrity & Versioning                            ${NC}"
echo -e "${CYAN}====================================================================${NC}"

info "Test 5: Data accessibility"
if echo "$STATUS_OUTPUT" | grep -q "Gain:"; then
    pass "DAC data accessible"
else
    fail "DAC data not accessible"
fi

if echo "$STATUS_OUTPUT" | grep -q "TX Power:"; then
    pass "PHY data accessible"
else
    fail "PHY data not accessible"
fi

if echo "$STATUS_OUTPUT" | grep -q "Preamble Format:"; then
    pass "PRACH data accessible"
else
    fail "PRACH data not accessible"
fi

info "Test 6: Version incrementing"
INITIAL_VER=$(echo "$STATUS_OUTPUT" | grep "^dac_status" | awk '{print $2}')
[ -z "$INITIAL_VER" ] && INITIAL_VER=0
sleep 7
STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
UPDATED_VER=$(echo "$STATUS_OUTPUT" | grep "^dac_status" | awk '{print $2}')
[ -z "$UPDATED_VER" ] && UPDATED_VER=0

if [ "$UPDATED_VER" -gt "$INITIAL_VER" ]; then
    pass "Versions incrementing ($INITIAL_VER -> $UPDATED_VER)"
else
    fail "Versions not updating"
fi

################################################################################
# Section 3: Subscriber Functionality
################################################################################
echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}  Section 3: Subscriber Registration & Notifications                ${NC}"
echo -e "${CYAN}====================================================================${NC}"

info "Test 7: Start subscribers"
timeout 5 $BUILD_DIR/subscriber_dac > /tmp/sub_dac.log 2>&1 &
SUB1_PID=$!
timeout 5 $BUILD_DIR/subscriber_phy_prach > /tmp/sub_phy.log 2>&1 &
SUB2_PID=$!
sleep 2

if ps -p $SUB1_PID > /dev/null 2>&1; then
    pass "DAC subscriber started (PID: $SUB1_PID)"
else
    fail "DAC subscriber failed"
fi

if ps -p $SUB2_PID > /dev/null 2>&1; then
    pass "PHY+PRACH subscriber started (PID: $SUB2_PID)"
else
    fail "PHY+PRACH subscriber failed"
fi

info "Test 8: Subscriber registration count"
sleep 2
STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
DAC_SUBS=$(echo "$STATUS_OUTPUT" | grep "^dac_status" | awk '{print $4}')
PHY_SUBS=$(echo "$STATUS_OUTPUT" | grep "^phy_status" | awk '{print $4}')

[ -z "$DAC_SUBS" ] && DAC_SUBS=0
[ -z "$PHY_SUBS" ] && PHY_SUBS=0

if [ "$DAC_SUBS" -ge "1" ]; then
    pass "DAC has subscribers ($DAC_SUBS)"
else
    fail "DAC has no subscribers"
fi

if [ "$PHY_SUBS" -ge "1" ]; then
    pass "PHY has subscribers ($PHY_SUBS)"
else
    fail "PHY has no subscribers"
fi

################################################################################
# Section 4: Stress Testing
################################################################################
echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}  Section 4: Stress Testing                                         ${NC}"
echo -e "${CYAN}====================================================================${NC}"

info "Test 9: Multiple subscribers (stress test)"
# Start stress subscribers
$BUILD_DIR/stress_subscriber 3 > /dev/null 2>&1 &
STRESS_PID=$!
sleep 3

if ps -p $STRESS_PID > /dev/null 2>&1; then
    STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
    TOTAL_SUBS=$(echo "$STATUS_OUTPUT" | awk '/^[a-z_]+status/ {sum+=$4} END {print sum}')
    if [ "$TOTAL_SUBS" -ge "5" ]; then
        pass "Multiple subscribers registered (total: $TOTAL_SUBS)"
    else
        fail "Expected at least 5 subscribers, got $TOTAL_SUBS"
    fi
    kill $STRESS_PID 2>/dev/null || true
else
    fail "Stress subscriber failed to start"
fi

################################################################################
# Section 5: Crash Recovery
################################################################################
echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}  Section 5: Crash Recovery & Robustness                            ${NC}"
echo -e "${CYAN}====================================================================${NC}"

info "Test 10: Publisher crash recovery"
# Kill one publisher
kill -9 $PUB1_PID 2>/dev/null || true
sleep 1

# Check system still works
STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
if echo "$STATUS_OUTPUT" | grep -q "phy_status"; then
    pass "System operational after publisher crash"
else
    fail "System failed after publisher crash"
fi

# Restart crashed publisher
$BUILD_DIR/publisher_dac_prach > /dev/null 2>&1 &
PUB1_PID=$!
sleep 2

if ps -p $PUB1_PID > /dev/null 2>&1; then
    pass "Publisher restarted successfully"
else
    fail "Publisher restart failed"
fi

info "Test 11: Subscriber crash - system stability"
kill -9 $SUB1_PID 2>/dev/null || true
sleep 1

# Publisher should continue working
STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
NEW_VER=$(echo "$STATUS_OUTPUT" | grep "^dac_status" | awk '{print $2}')
sleep 2
STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
NEWER_VER=$(echo "$STATUS_OUTPUT" | grep "^dac_status" | awk '{print $2}')

if [ "$NEWER_VER" -gt "$NEW_VER" ]; then
    pass "Publishers continue after subscriber crash"
else
    fail "Publishers stalled after subscriber crash"
fi

################################################################################
# Section 6: Data Pool Management
################################################################################
echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}  Section 6: Memory & Data Pool Management                          ${NC}"
echo -e "${CYAN}====================================================================${NC}"

info "Test 12: Data pool usage"
STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)
DATA_USAGE=$(echo "$STATUS_OUTPUT" | grep "Data Pool Usage:" | awk '{print $4}')

if [ ! -z "$DATA_USAGE" ] && [ "$DATA_USAGE" -gt "0" ]; then
    pass "Data pool in use ($DATA_USAGE bytes)"
else
    fail "Data pool usage not detected"
fi

info "Test 13: Shared memory size check"
if [ -f /dev/shm/typed_status_shm ]; then
    SHM_SIZE=$(stat -c%s /dev/shm/typed_status_shm)
    # Should be around 1MB (1048576 + metadata)
    if [ "$SHM_SIZE" -gt "1000000" ]; then
        pass "Shared memory size appropriate ($SHM_SIZE bytes)"
    else
        fail "Shared memory size unexpected ($SHM_SIZE bytes)"
    fi
else
    fail "Shared memory not found"
fi

################################################################################
# MESSAGE STATISTICS
################################################################################
echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}                    MESSAGE STATISTICS                              ${NC}"
echo -e "${CYAN}====================================================================${NC}"
echo ""

STATUS_OUTPUT=$($BUILD_DIR/mon_status 2>&1)

echo "Status Statistics:"
printf "%-15s %10s %10s %10s\n" "STATUS" "PUBLISHED" "NOTIFIED" "VERSION"
echo "---------------------------------------------------------------"

TOTAL_PUBLISHED=0
TOTAL_NOTIFIED=0

for status_name in dac_status phy_status prach_status; do
    if echo "$STATUS_OUTPUT" | grep -q "^$status_name"; then
        PUBLISHED=$(echo "$STATUS_OUTPUT" | grep "^$status_name" | awk '{print $5}')
        NOTIFIED=$(echo "$STATUS_OUTPUT" | grep "^$status_name" | awk '{print $6}')
        VERSION=$(echo "$STATUS_OUTPUT" | grep "^$status_name" | awk '{print $2}')
        [ -z "$PUBLISHED" ] && PUBLISHED=0
        [ -z "$NOTIFIED" ] && NOTIFIED=0
        [ -z "$VERSION" ] && VERSION=0
        printf "%-15s %10d %10d %10d\n" "$status_name" "$PUBLISHED" "$NOTIFIED" "$VERSION"
        TOTAL_PUBLISHED=$((TOTAL_PUBLISHED + PUBLISHED))
        TOTAL_NOTIFIED=$((TOTAL_NOTIFIED + NOTIFIED))
    fi
done
echo "---------------------------------------------------------------"
printf "%-15s %10d %10d %10s\n" "TOTAL" "$TOTAL_PUBLISHED" "$TOTAL_NOTIFIED" "-"
echo ""

################################################################################
# CLEANUP TEST
################################################################################
echo ""
echo -e "${CYAN}====================================================================${NC}"
echo -e "${CYAN}  Section 7: Cleanup & Resource Management                          ${NC}"
echo -e "${CYAN}====================================================================${NC}"

info "Test 14: Clean shutdown"
cleanup
sleep 1

if [ ! -f /dev/shm/typed_status_shm ]; then
    pass "Shared memory cleaned up"
else
    fail "Shared memory still exists"
fi

if ! ps -p $PUB1_PID > /dev/null 2>&1 && ! ps -p $PUB2_PID > /dev/null 2>&1; then
    pass "All publishers stopped"
else
    fail "Some publishers still running"
fi

################################################################################
# SUMMARY
################################################################################
echo ""
echo -e "${BLUE}====================================================================${NC}"
echo -e "${BLUE}                        TEST SUMMARY                                ${NC}"
echo -e "${BLUE}====================================================================${NC}"
echo ""
echo -e "  Total Tests:  $(($TESTS_PASSED + $TESTS_FAILED))"
echo -e "  ${GREEN}[+] Passed:   $TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "  ${RED}[-] Failed:   $TESTS_FAILED${NC}"
else
    echo -e "  ${GREEN}[-] Failed:   $TESTS_FAILED${NC}"
fi
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}====================================================================${NC}"
    echo -e "${GREEN}                     [+] ALL TESTS PASSED [+]                       ${NC}"
    echo -e "${GREEN}====================================================================${NC}"
    echo ""
    exit 0
else
    echo -e "${RED}====================================================================${NC}"
    echo -e "${RED}                     [-] SOME TESTS FAILED [-]                      ${NC}"
    echo -e "${RED}====================================================================${NC}"
    echo ""
    exit 1
fi
