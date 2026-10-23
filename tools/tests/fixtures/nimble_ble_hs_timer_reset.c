static void
ble_hs_timer_reset(uint32_t ticks)
{
    int rc;

    if (!ble_hs_is_enabled()) {
        ble_npl_callout_stop(&ble_hs_timer);
        ble_npl_callout_deinit(&ble_hs_timer);
    } else {
        rc = ble_npl_callout_reset(&ble_hs_timer, ticks);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}
