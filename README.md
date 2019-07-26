# Bluetooth Services for mangOH

## What is this?
This app currently provides a battery service and an immediate alert service for the mangOH Yellow.

## Quirks
You have to run `echo "_ app.bluezServices rwx" > /sys/fs/smackfs/load2` to give dbus-1 the ability
to talk to the bluezServices app.
