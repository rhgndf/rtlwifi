# Not implemented

The RTL8192SU port keeps the shared USB changes minimal. The items below either
need shared USB-core behavior changes or were reverted to preserve existing
RTL8192CU/RTL8192DU behavior.

## Retained USB-core dependencies

These shared changes remain because the SU frontend cannot implement them
locally:

- `usb_rx_hdl` is dispatched from the USB RX worker, transferring ownership of
  the complete skb to the frontend. RTL8192SU needs this to split S-format RX
  records and consume firmware events.
- `RTL_USB_MAX_BULKOUT_NUM` is 8 so the RTL8192SU 11-endpoint layout can retain
  all bulk-OUT endpoint addresses.

## Reverted receive changes

- **Short raw RX records:** `_rtl_rx_completed()` retains the existing minimum
  of an RX descriptor plus an IEEE 802.11 header and its existing header-based
  padding calculation. A 24-byte descriptor followed by only the 8-byte S C2H
  header can therefore be discarded before `rtl92su_rx_hdl()` receives it.
  Supporting this safely requires a frontend-specific raw-RX minimum/padding
  contract in the USB core; `usb_rx_hdl != NULL` is insufficient because the
  existing RTL8192CU handler also uses that callback.
- **Second bulk-IN endpoint:** `struct rtl_usb` retains one `in_ep`. All eight RX
  URBs use endpoint 3, including the RTL8192SU 11-endpoint layout; bulk-IN
  endpoint 9 is ignored. Using both endpoints requires shared endpoint storage,
  bulk-only enumeration, and per-URB endpoint selection in the USB core.
- **RX startup unwind:** `rtl_usb_start()` retains its existing behavior if RX
  URB setup or submission fails after hardware initialization. Disabling the
  frontend and restoring stopped state on that path requires a shared lifecycle
  change and was reverted.

## Reverted transmit changes

`_usb_tx_post()` and `rtl92su_tx_post_hdl()` retain the existing zero-return
completion contract. The core ignores the callback return value and always
continues through generic descriptor removal and status reporting.
Consequently:

- RTL8192SU cannot own a completed data skb or report chip-specific completion
  status from `rtl92su_tx_post_hdl()`.
- The generic path marks ACK even when a completed URB reports an error.
- Pending frames drained during cleanup are also marked ACK.
- A completion observed after USB STOP returns without releasing its skb.
- URB setup/submission failures directly free the skb without reporting TX
  status to mac80211.

Supporting chip-owned completion or truthful ACK/NO_ACK status requires changing
the shared USB-core ownership contract, so those changes were reverted.
