# Windows integration status

No Windows WiFiCx / NetAdapterCx kernel driver is implemented in this repository.
The current driver core uses Linux kernel APIs and is shared by Linux and Android.
Windows can reuse the protocol definitions in shared after WDK-side adaptation;
it cannot directly compile the Linux cfg80211, USB, locking or skb implementations.

A future Windows port needs its own WDF lifecycle, USB/SDIO transport, request
cancellation, synchronization, WiFiCx/NetAdapterCx integration and signed test builds.
Do not bind a generic WinUSB/libusb driver to a device currently owned by WHC or a VM.
