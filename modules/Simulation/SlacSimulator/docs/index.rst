SLAC Simulator
==============

The EVSE SLAC interface publishes ``ev_mac_address`` on each successful match
before ``D-LINK_READY``. The default is
``AA:BB:CC:DD:EE:FF``; set the module configuration to another full MAC address
to exercise MAC-specific EvseManager policies in SIL.
