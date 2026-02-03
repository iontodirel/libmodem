#!/usr/bin/env python3

# Install dependencies using: pip install aprs3

import aprs

with aprs.TCPKISS("127.0.0.1", 1234) as kiss_tnc:
    frame = aprs.APRSFrame.from_str('W7ION-5>T7SVVQ,WIDE1-1,WIDE2-1:`2(al"|[/>"3u}hello world^')
    kiss_tnc.write(frame)