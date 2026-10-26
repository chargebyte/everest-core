.. _everest_modules_handwritten_AST_DC650:

.. *********
.. AST_DC650
.. *********

Driver for the AST DC meter C protocol described in ``rs485_guide_dc-meter_en_v4_9.pdf``
and ``rs485_guide_detached_display_en_v6_5.pdf``. It provides ``main`` (``powermeter``)
and ``temperature`` (``temperature_sensor``). The ST display-management protocol,
pairing and assembly-mode provisioning are outside this driver.

Configuration
=============

``main.protocol_variant`` accepts:

- ``auto`` (default): read ``0x1531``; a 14-byte payload selects DC-Meter 4.9,
  a 38-byte payload selects Detached Display 6.5. Unknown layouts fail explicitly.
- ``dc_meter_v4_9``: explicitly select the 13-character charge point ID layout.
- ``detached_display_v6_5``: explicitly select the 37-character charge point ID layout.

Both explicit modes still validate the device response layout. No start command is
used to probe firmware capabilities. Older, undocumented firmware layouts are not
automatically supported. Only one meter may use the broadcast address.

Configure ``serial_port``, ``baudrate``, ``parity``, ``powermeter_device_id`` and
``ignore_echo`` for the physical connection. With ``ignore_echo``, the exact transmitted
frame is removed from reception without consuming the following response. The
existing ``rs485_direction_gpio`` option is retained for configuration compatibility;
it does not implement GPIO direction control. Use hardware/kernel-managed RS485
direction switching.

``gmt_offset_quarter_hours`` applies to time synchronization and transaction start;
it is independent of the host timezone. UTC seconds use the Unix epoch.

Initial response timeout is 2500 ms; the frame deadline also accounts for baudrate and
maximum escaped response size.

Measurements
============

The regular measurement cycle reads device voltage (``0x0137``), signed current
(``0x0131``), device import/export power (``0x0138``/``0x0139``) and device import/export
energy (``0x0170``/``0x0173``). Published power is import minus export. A failed mandatory
read suppresses the dataset rather than publishing stale values with a fresh
timestamp. No diagnostic switch is needed for energy acquisition. Calculations and diagnostic
energy values retain double precision; the generated EVerest numeric interface
fields use their existing float representation.

The meter may report zero device voltage outside an active transaction. Device
measurements are distinct from mains measurements: the documented default OCMF
configuration places **mains energy** in its RD readings. The driver does not
rewrite the meter's OCMF configuration.

Internal temperature (``0x4136``, signed tenths of a degree Celsius) is read once per
cycle and published on both ``main.powermeter.temperatures`` and
``temperature.temperatures``. Both use ``identification: meter_internal`` and
``location: DC meter``. Failed temperature reads do not publish a substitute value;
an explicitly unsupported temperature command is disabled for the process lifetime.

Signed Transactions
===================

Call ``stop_transaction("")`` to clear a possible transaction left over from an earlier
process before starting a transaction. Cleanup accepts an inactive meter only when
the meter's error record explicitly identifies that condition; a generic rejected
command is not treated as success.

- 4.9 sends ``<tariff_text><=><transaction_id>`` inside the ``#TT#`` markers. The complete
  tariff field must fit in 250 ASCII bytes. The ID suffix is checked in returned OCMF.
- 6.5 sends the tariff and a null-terminated transaction ID in separate fields. OCMF
  is retrieved by transaction ID with ``0xA102``. If that command is explicitly
  unsupported, the driver tries the common last-OCMF command but still requires a
  matching signed tariff suffix. Firmware without either association mechanism
  returns an error instead of attributing an unverified record to the transaction.

Identification status and all identification types are mapped to AST values.
Identification data is limited to 40 ASCII bytes; transaction IDs to 36 printable
ASCII bytes. Tariff text cannot contain the reserved ``#TT#`` or ``<=>`` markers.
Overlong or invalid input is rejected without truncation. The PDFs do not define
start fields for identification level or identification flags. These are not encoded
in ``userData``; that optional 6.5 field remains empty.

The charge point ID is read from the meter. A difference from ``TransactionReq.evse_id``
is logged; the stored ID is used. The driver does not enter assembly mode or
change the configured ID.

Repeated starts of the active ID and completed stops are idempotent within the
process. A concurrent ID is rejected. A timed-out write leaves the transaction state
uncertain and requires explicit cleanup; start/stop writes are never blindly retried.
A successful stop whose OCMF read fails can retry readout without repeating the stop.
Transient OCMF readiness statuses are polled every 250 ms for up to five seconds;
each individual response retains its transport deadline.

The meter public key (``0x0211``, hexadecimal ASCII) is published via ``public_key_ocmf``
and attached to returned OCMF. Reports must contain JSON readings and a signature;
original signed text is returned unchanged. The driver checks report structure and
association, but does not cryptographically verify signatures or create local ones.
Verify hardware reports with the meter public key as part of commissioning.

Diagnostics and Logging
=======================

``publish_device_data`` adds device, mains, line-loss and transaction measurements on
``<meter_id>/device_data``. ``publish_device_diagnostics`` adds identification, revisions,
keys and OCMF configuration on ``<meter_id>/device_diagnostics``, and meter logs/errors
on ``<meter_id>/logging``. These existing topic suffixes remain available. Their
snapshot interval is ten seconds; optional unsupported commands are disabled.

``EVLOG_info`` reports initialization, protocol selection, optional capabilities,
key publication, transaction completion and recovery. A differing charge point ID
uses ``EVLOG_warning``. Operational errors use ``EVLOG_error``; identical polling errors
are suppressed until the condition changes. Raw frames are available only at
``EVLOG_debug``. Debug MQTT switches do not control normal operational logs.

Validation
==========

Build ``AST_DC650`` and ``everest-core_ast_dc650_tests`` with testing enabled, then run:

.. code-block:: console

   ctest --test-dir build -R ast_dc650 --output-on-failure

The tests cover both layouts, framing/CRC/escaping, malformed messages, signed
measurements, temperature failures and transaction state/readout behavior. Hardware
acceptance still requires a start/stop cycle and independent OCMF signature
verification with each firmware variant.
