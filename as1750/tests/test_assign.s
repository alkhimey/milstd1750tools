	name test_assign

; AS0, instruction page reg 0x0 to 0xA => 0x10,0x11,0x12,...,0x1A :
	assign	0.0i, 10, B

; AS0, operand page reg 0x0 to 0xA => 0x40,0x41,0x42,...,0x4A :
	assign	0.0o, 40, B

; AS0, operand page reg 0xB to 0xE => 0x1B,0x1C,0x1D,0x1E :
	assign	0.Bo, 1B, 4

; AS0, operand page reg 0xF => 0x4F :
	assign	0.Fo, 4F, 1

; AS1, operand page reg 0xF => 0x1F :
	assign	1.Fo, 1F, 1

	normal
	org	0x10000
test_assign	br	0

	konst
x	dataf	1.0

  end