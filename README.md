# What

This repo currently contains a small tool for flipping an M.2 XMM7360 modem,
like the Fibocom L850-GL, from PCIe into USB mode.
This allows you to talk to it with `mbimcli`, `ModemManager` etc.

# Usage

This script needs [the acpi_call kernel module](https://github.com/mkottman/acpi_call).
You can build and install it like this:

```
git clone https://github.com/mkottman/acpi_call.git
cd acpi_call
make && sudo make install
```

Then, you can simply run this script:

```
sudo ./xmm2usb
```

After 5-10 seconds, your new USB device should appear.

# MBIM Switch

My modem came up initially as a 3x ACM + 3x NCM USB device.
To switch to MBIM mode I ran:

```
sudo screen /dev/ttyACM0
```

and issued

```
AT+GTUSBMODE?
AT+GTUSBMODE=7
AT+CFUN=15
```

Take note of the USB mode reported after the first command in case you want to put it back later...!

(There is an old L8 family AT command doc floating around saying that mode 2 is MBIM. It does not apply to this modem.)

# Next?

My modem, in my Thinkpad T490, seems to be stuck in flight mode (`AT+CFUN?` returns `+CFUN: 4,0` and can't be changed).
In MBIM mode, the power state is off, and all attempts to turn it on return Busy.
In ACM mode, it can be "enabled" by ModemManager, but I get a No network error when I try and connect.
This is a bit mysterious because the GPIO that enables the modem should be set appropriately (according to the thinkpad_acpi driver; it's the `SWAN`/`GWAN` interface).
I don't particularly feel like opening my laptop again at the moment though to check the line status.
Please let me know how you get on with this and other machines.

# PCI

I did start doing some reverse engineering and writing a PCI driver, just for fun.
So far you can send AT commands to a couple of ports.
I haven't figured out where to stick an MBIM command to get a response though.
