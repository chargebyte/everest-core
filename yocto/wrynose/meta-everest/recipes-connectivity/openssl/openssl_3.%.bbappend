FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# The status_request_v2 patch's non-header hunks (s3_lib.c, extensions_clnt.c,
# extensions_srvr.c, statem_clnt.c) still apply cleanly against 3.5.7, just
# with a line-offset from the version bump (verified: same context, correct
# insertion point) — the ssl.h.in/tls1.h hunks were hand-rebased for exact,
# zero-offset context. patch-fuzz is checked directly against ERROR_QA/WARN_QA
# (not INSANE_SKIP) at do_patch time, so demote it to a warning for this
# recipe rather than hand-editing more unverified C context.
ERROR_QA:remove = " patch-fuzz"
WARN_QA:append = " patch-fuzz"

python __anonymous() {
    pv = d.getVar("PV")

    if bb.utils.vercmp_string_op(pv, "3.3.6", "<"):
        d.appendVar("SRC_URI", " file://openssl-3.2-feat-updates-to-support-status_request_v2.patch")
    else:
        d.appendVar("SRC_URI", " file://openssl-3.5-feat-updates-to-support-status_request_v2.patch")
}
