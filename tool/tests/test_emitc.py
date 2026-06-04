from htgl.emitc import emit_c_array

def test_emits_symbol_and_length():
    src = emit_c_array(b"\x01\x02\xff", "hello_ui")
    assert "const unsigned char hello_ui_blob[] = {" in src
    assert "0x01, 0x02, 0xff," in src
    assert "const unsigned int hello_ui_blob_len = 3;" in src

def test_round_trip_values():
    data = bytes(range(0, 20))
    src = emit_c_array(data, "x")
    for b in data:
        assert f"0x{b:02x}" in src
