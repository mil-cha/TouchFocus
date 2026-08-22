#!/usr/bin/env python3
"""TouchFocus BLE-to-UDP bridge. Does not access GPIO."""
import json
import socket
import select
import threading
import time
import dbus
import dbus.service
import dbus.mainloop.glib
from gi.repository import GLib

BLUEZ = "org.bluez"
GATT_SERVICE = "org.bluez.GattService1"
GATT_CHRC = "org.bluez.GattCharacteristic1"
PROPS = "org.freedesktop.DBus.Properties"
SERVICE_UUID = "7a8b0001-6f32-4f1f-9d32-54f6a4a10001"
COMMAND_UUID = "7a8b0002-6f32-4f1f-9d32-54f6a4a10001"
STATUS_UUID = "7a8b0003-6f32-4f1f-9d32-54f6a4a10001"

udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
status_udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
status_udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
status_udp.bind(("0.0.0.0", 40001))
status_udp.settimeout(0.5)

class Service(dbus.service.Object):
    def __init__(self, bus):
        self.path = "/org/touchfocus/service0"
        super().__init__(bus, self.path)
    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        return {"UUID": SERVICE_UUID, "Primary": True,
                "Characteristics": dbus.Array([command.path, status.path], signature="o")}

class Characteristic(dbus.service.Object):
    def __init__(self, bus, path, uuid, flags):
        self.path, self.uuid, self.flags = path, uuid, flags
        super().__init__(bus, path)
    @dbus.service.method(PROPS, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        return {"Service": dbus.ObjectPath(service.path), "UUID": self.uuid,
                "Flags": dbus.Array(self.flags, signature="s"), "Descriptors": dbus.Array([], signature="o")}

class Command(Characteristic):
    @dbus.service.method(GATT_CHRC, in_signature="aya{sv}")
    def WriteValue(self, value, options):
        payload = bytes(value)
        json.loads(payload.decode())  # reject malformed commands
        udp.sendto(payload, ("127.0.0.1", 40000))

class Status(Characteristic):
    notifying = False
    @dbus.service.method(GATT_CHRC)
    def StartNotify(self): self.notifying = True
    @dbus.service.method(GATT_CHRC)
    def StopNotify(self): self.notifying = False
    def publish(self, payload):
        if self.notifying:
            self.PropertiesChanged(GATT_CHRC,
                {"Value": dbus.Array(payload, signature="y")}, [])
    @dbus.service.signal(PROPS, signature="sa{sv}as")
    def PropertiesChanged(self, interface, changed, invalidated): pass

class Application(dbus.service.Object):
    def __init__(self, bus): super().__init__(bus, "/")
    @dbus.service.method("org.freedesktop.DBus.ObjectManager", out_signature="a{oa{sa{sv}}}")
    def GetManagedObjects(self):
        return {service.path: {GATT_SERVICE: service.GetAll(GATT_SERVICE)},
                command.path: {GATT_CHRC: command.GetAll(GATT_CHRC)},
                status.path: {GATT_CHRC: status.GetAll(GATT_CHRC)}}

class Advertisement(dbus.service.Object):
    def __init__(self, bus):
        self.path = "/org/touchfocus/advertisement0"
        super().__init__(bus, self.path)
    @dbus.service.method(PROPS, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        return {"Type": "peripheral", "ServiceUUIDs": dbus.Array([SERVICE_UUID], signature="s"),
                "LocalName": "TouchFocus-RPi", "Discoverable": True}
    @dbus.service.method("org.bluez.LEAdvertisement1")
    def Release(self): pass

def status_loop():
    while True:
        try:
            ready, _, _ = select.select([status_udp, udp], [], [], 0.5)
            for sock in ready:
                payload, _ = sock.recvfrom(512)
                json.loads(payload.decode())
                GLib.idle_add(status.publish, payload)
        except Exception:
            pass

def find_adapter(bus):
    objects = dbus.Interface(bus.get_object(BLUEZ, "/"), "org.freedesktop.DBus.ObjectManager").GetManagedObjects()
    for path, interfaces in objects.items():
        if "org.bluez.GattManager1" in interfaces and "org.bluez.LEAdvertisingManager1" in interfaces:
            return path
    raise RuntimeError("No BLE adapter with GATT/advertising support")

if __name__ == "__main__":
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    service = Service(bus)
    command = Command(bus, "/org/touchfocus/service0/command0", COMMAND_UUID, ["write", "write-without-response"])
    status = Status(bus, "/org/touchfocus/service0/status0", STATUS_UUID, ["notify"])
    app, advertisement = Application(bus), Advertisement(bus)
    adapter = find_adapter(bus)
    gatt_manager = dbus.Interface(bus.get_object(BLUEZ, adapter),
                                  "org.bluez.GattManager1")
    advertising_manager = dbus.Interface(bus.get_object(BLUEZ, adapter),
                                         "org.bluez.LEAdvertisingManager1")
    mainloop = GLib.MainLoop()

    def registration_failed(error):
        print(f"[BLE] Registration failed: {error}", flush=True)
        mainloop.quit()

    def advertisement_ready():
        print("[BLE] TouchFocus-RPi ready", flush=True)
        threading.Thread(target=status_loop, daemon=True).start()

    def application_ready():
        print("[BLE] GATT application registered", flush=True)
        advertising_manager.RegisterAdvertisement(
            advertisement.path, {},
            reply_handler=advertisement_ready,
            error_handler=registration_failed)

    gatt_manager.RegisterApplication(
        "/", {}, reply_handler=application_ready,
        error_handler=registration_failed)
    mainloop.run()
