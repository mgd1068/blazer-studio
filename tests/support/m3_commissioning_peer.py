#!/usr/bin/env python3
"""Independent bounded XY plant. Own PTY only; never opens supplied devices.

Counter reset deliberately does not move the head. G54, G90/G91 and G20/G21
are interpreted independently of Blazer's encoder/preflight. A negative machine
space and shifted G54 expose invented positive-coordinate assumptions.
"""
from runtime_pty_peer import Peer
import re

class Plant(Peer):
    def __init__(self):
        super().__init__()
        self.actual = [-70.0, -80.0]
        self.bias = [70.0, 80.0]
        self.offset = [-180.0, -130.0]
        self.units = 1.0
        self.absolute = True
        self.power = 0.0
        self.spindle = 5
        self.settings = {1:25,3:5,5:0,13:0,20:0,21:1,22:1,23:7,24:800,25:2000,
                         26:30,27:2,30:1000,31:0,32:1,100:80,101:80,110:7000,
                         111:7000,120:800,121:240,130:200,131:150}
        self.startup = ['', '']
        self.zero_seen = self.hold_zero_modal_ack = self.pending_zero_modal_ack = False
        self.omit = -1
        self.alarm_after_reset = False

    def status(self):
        self.position = [a+b for a,b in zip(self.actual,self.bias)]
        super().status()

    def command(self, line):
        if line == '$I': self.reply('[VER:1.3a.20211103:synthetic-M3]\r\n')
        elif line == '$$':
            self.reply(''.join(f'${k}={v}\r\n' for k,v in self.settings.items() if k != self.omit))
        elif line == '$N': self.reply(f'$N0={self.startup[0]}\r\n$N1={self.startup[1]}\r\n')
        elif line == '$#': self.reply(f'[G54:{self.offset[0]},{self.offset[1]},0]\r\n[G92:0,0,0]\r\n[TLO:0]\r\n')
        elif line == '$G': self.reply(f'[GC:G0 G54 G17 G{21 if self.units==1 else 20} G{90 if self.absolute else 91} G94 M{self.spindle} M9 T0 F0 S{self.power:g}]\r\n')
        elif line == '$H':
            self.actual = [-198.0, -148.0]; self.bias = [0.0,0.0]
            self.state = 'Idle'; self.event('homed',list(self.actual))
        elif line.startswith('$'): raise ValueError('unsupported plant command')
        else:
            # Parenthesized comments are non-executable standard GCode.
            line = re.sub(r'\([^()]*\)', '', line).strip()
            words = re.findall(r'([A-Z])([+-]?(?:\d+(?:\.\d*)?|\.\d+))',line)
            if ''.join(k+v for k,v in words) != line.replace(' ',''): raise ValueError('malformed command')
            codes = [int(float(v)) for k,v in words if k=='G']
            if any(v not in [0,1,4,10,17,20,21,54,90,91,94] for v in codes): raise ValueError('unexpected modal code')
            for k,v in words:
                n=float(v)
                if k=='G' and n==20:self.units=25.4
                if k=='G' and n==21:self.units=1.0
                if k=='G' and n==90:self.absolute=True
                if k=='G' and n==91:self.absolute=False
                if k=='M':
                    if n not in (3,4,5): raise ValueError('unexpected output')
                    self.spindle=int(n)
                if k=='S':self.power=n
            if self.spindle in (3,4) and self.power>0:self.event('powered',self.power)
            if 10 in codes:
                if not re.fullmatch(r'G10 L20 P1 X[+-]?[0-9.]+ Y[+-]?[0-9.]+',line):raise ValueError('unexpected G10')
                requested={k:float(v) for k,v in words if k in ('X','Y')}
                self.offset=[a+b-requested[k] for a,b,k in zip(self.actual,self.bias,('X','Y'))]
                self.zero_seen=True
            elif any(k in ('X','Y') for k,v in words):
                target=list(self.actual)
                for k,v in words:
                    if k not in ('X','Y'):continue
                    i=0 if k=='X' else 1
                    target[i]=float(v)*self.units+self.offset[i]-self.bias[i] if self.absolute else target[i]+float(v)*self.units
                if not (-200<=target[0]<=0 and -150<=target[1]<=0):
                    self.event('collision',target);self.reply('ALARM:1\r\n');return
                moved=target!=self.actual
                self.actual=target
                if moved:self.event('physicalMove',target)
            if self.state!='Alarm':self.state='Idle'
        if self.ack_enabled:
            if line=='$G' and self.zero_seen and self.hold_zero_modal_ack:
                self.pending_zero_modal_ack=True;self.zero_seen=False;self.event('zeroAckHeld')
            else:self.reply('ok\r\n');self.event('ack')

    def receive(self,data):
        for byte in data:
            if byte==ord('?'):self.event('query');self.status()
            elif byte in (ord('!'),ord('~'),0x18):
                self.event('realtime',byte)
                if byte==0x18:
                    self.input.clear();self.bias=[-v for v in self.actual]
                    self.zero_seen=self.pending_zero_modal_ack=False
                    self.state='Alarm' if self.alarm_after_reset else 'Idle';self.spindle=5;self.power=0
                    if self.reset_enabled:self.reply("Grbl 1.3a ['$' for help]\r\n")
                elif byte==ord('!'):self.state='Hold'
                else:self.state='Run'
            elif byte in (10,13):
                if self.input:
                    line=self.input.decode('ascii');self.input.clear();self.event('command',line);self.command(line)
            elif byte>=0x80:self.event('overrideByte',byte)
            else:
                self.input.append(byte)
                if len(self.input)>256:raise ValueError('plant command bound')

    def operate(self,request):
        if request.get('action')=='releaseZeroAck':
            if not self.pending_zero_modal_ack:return {'accepted':False}
            self.pending_zero_modal_ack=False;self.reply('ok\r\n');self.event('ack');self.event('zeroAckReleased')
            return {'accepted':True}
        if request.get('action')=='plantConfig':
            if 'manualPosition' in request:
                target=request['manualPosition']
                if len(target)!=2:raise ValueError('manual fixture XY')
                reported=[a+b for a,b in zip(self.actual,self.bias)]
                self.actual=list(target);self.bias=[r-a for r,a in zip(reported,self.actual)];self.event('handRelocated',self.actual)
            if 'doorSubstate' in request:
                sub=request['doorSubstate']
                self.state='Idle' if sub is None else 'Door:'+str(int(sub))
            if 'holdZeroModalAck' in request:self.hold_zero_modal_ack=bool(request['holdZeroModalAck'])
            if 'alarmAfterReset' in request:self.alarm_after_reset=bool(request['alarmAfterReset'])
            if 'startup' in request:self.startup[0]=request['startup']
            if 'omit' in request:self.omit=int(request['omit'])
            if 'setting' in request:
                key,value=request['setting'];self.settings[int(key)]=float(value)
            return {'accepted':True}
        result=super().operate(request)
        if request.get('action')=='snapshot':result.update(actual=self.actual,reported=[a+b for a,b in zip(self.actual,self.bias)],offset=self.offset)
        return result

if __name__=='__main__':
    peer=Plant()
    try:peer.run()
    finally:peer.close()
