# PlatformIO pre-build script: patch NimBLE's Security Manager so the app can
# refuse pairing per connection (ble_sm_app_allow_pairing, weak default =
# allow). The remote refuses pairing on its Mac advertising set: macOS pairs
# any peer that has a HID service and then forces keyboard link parameters
# (2 s supervision timeout) that drop the Mac link. Idempotent.
import os

Import("env")  # noqa: F821

MARK = "ble_sm_app_allow_pairing"
OLD = """        conn = ble_hs_conn_find_assert(proc->conn_handle);
        if (conn->bhc_flags & BLE_HS_CONN_F_MASTER) {
            res->sm_err = BLE_SM_ERR_CMD_NOT_SUPP;
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_CMD_NOT_SUPP);
        } else if (MYNEWT_VAL(BLE_SM_LVL) == 1) {"""
NEW = """        conn = ble_hs_conn_find_assert(proc->conn_handle);
        struct ble_hs_conn_addrs dvh_addrs;
        ble_hs_conn_addrs(conn, &dvh_addrs);
        if (conn->bhc_flags & BLE_HS_CONN_F_MASTER) {
            res->sm_err = BLE_SM_ERR_CMD_NOT_SUPP;
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_CMD_NOT_SUPP);
        } else if (!ble_sm_app_allow_pairing(conn_handle, &dvh_addrs.our_ota_addr)) {
            res->sm_err = BLE_SM_ERR_PAIR_NOT_SUPP;
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_PAIR_NOT_SUPP);
        } else if (MYNEWT_VAL(BLE_SM_LVL) == 1) {"""
DECL = """
/* dvh patch: the app may refuse pairing per connection (called under the host lock). */
__attribute__((weak)) int ble_sm_app_allow_pairing(uint16_t conn_handle, const ble_addr_t *our_addr)
{
    (void)conn_handle;
    (void)our_addr;
    return 1;
}

static void
ble_sm_pair_req_rx("""

libdeps = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")  # noqa: F821
path = os.path.join(libdeps, "NimBLE-Arduino", "src", "nimble", "nimble", "host", "src", "ble_sm.c")
if not os.path.exists(path):
    print("patch_nimble: %s not found (libraries not installed yet?) — build again" % path)
else:
    src = open(path).read()
    if MARK not in src:
        if OLD not in src or "\nstatic void\nble_sm_pair_req_rx(" not in src:
            raise SystemExit("patch_nimble: NimBLE ble_sm.c changed, update scripts/patch_nimble.py")
        src = src.replace(OLD, NEW).replace("\nstatic void\nble_sm_pair_req_rx(", DECL, 1)
        open(path, "w").write(src)
        print("patch_nimble: patched " + path)
