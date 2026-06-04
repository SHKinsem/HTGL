from htgl.colors import to_rgb565

def test_named_black_and_white():
    assert to_rgb565("black") == 0x0000
    assert to_rgb565("white") == 0xFFFF

def test_pure_red_green_blue():
    assert to_rgb565("#ff0000") == 0xF800
    assert to_rgb565("#00ff00") == 0x07E0
    assert to_rgb565("#0000ff") == 0x001F

def test_short_hex():
    assert to_rgb565("#f00") == 0xF800

def test_case_and_whitespace_insensitive():
    assert to_rgb565("  #00FF00 ") == 0x07E0

def test_unknown_defaults_black():
    assert to_rgb565("not-a-color") == 0x0000
