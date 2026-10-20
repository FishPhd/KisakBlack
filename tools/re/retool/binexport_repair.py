"""Make Ghidra BinExport files loadable by BinDiff 8.

BinDiff reads a flow graph's entry with Instruction.address() and ignores the
format's rule that a missing address means "continues from the previous
instruction". One such entry becomes address 0, and AttachFlowGraph aborts the
whole comparison. Edges whose basic blocks are not members of that flow graph
are also rejected and then silently attached to the function's first block.
"""
from __future__ import annotations


def _read_varint(buf, index):
    value = shift = 0
    while True:
        byte = buf[index]
        index += 1
        value |= (byte & 0x7F) << shift
        if byte < 128:
            return value, index
        shift += 7


def _encode_varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def _encode_key(field, wire):
    return _encode_varint((field << 3) | wire)


def _parse_fields(buf):
    index, end, fields = 0, len(buf), []
    while index < end:
        key, index = _read_varint(buf, index)
        field, wire = key >> 3, key & 7
        if wire == 0:
            value, index = _read_varint(buf, index)
        elif wire == 2:
            length, index = _read_varint(buf, index)
            value = buf[index:index + length]
            index += length
        elif wire == 1:
            value = buf[index:index + 8]
            index += 8
        elif wire == 5:
            value = buf[index:index + 4]
            index += 4
        else:
            raise ValueError(f"Unsupported protobuf wire type {wire}")
        fields.append((field, wire, value))
    return fields


def _encode_fields(fields):
    parts = []
    for field, wire, value in fields:
        parts.append(_encode_key(field, wire))
        if wire == 0:
            parts.append(_encode_varint(value))
        elif wire == 2:
            parts.append(_encode_varint(len(value)))
            parts.append(value)
        else:
            parts.append(value)
    return b"".join(parts)


def _messages(fields, number):
    return [_parse_fields(value) for field, wire, value in fields if field == number and wire == 2]


def _first_varint(fields, number):
    for field, wire, value in fields:
        if field == number and wire == 0:
            return value
    return None


def _block_instruction(block):
    for field, wire, value in block:
        if field == 1 and wire == 2:
            return _first_varint(_parse_fields(value), 1)
    return None


def repair_binexport(data):
    """Return BinExport bytes BinDiff can attach, plus a small change count."""
    if isinstance(data, memoryview):
        data = bytes(data)
    top = _parse_fields(data)
    instructions = _messages(top, 5)
    blocks = _messages(top, 6)
    call = next((value for field, wire, value in top if field == 8 and wire == 2), b"")
    vertices = set()
    for field, wire, value in _parse_fields(call):
        if field == 1 and wire == 2:
            vertices.add(_first_varint(_parse_fields(value), 1) or 0)

    addresses = []
    cursor = 0
    explicit = []
    for instruction in instructions:
        raw = next((value for field, wire, value in instruction if field == 5 and wire == 2), b"")
        stated = _first_varint(instruction, 1)
        explicit.append(stated is not None)
        if stated is not None:
            cursor = stated
        addresses.append(cursor)
        cursor += len(raw)

    starts = [_block_instruction(block) for block in blocks]
    rewritten = list(instructions)
    addresses_written = 0
    for start in starts:
        if start is None or explicit[start] or addresses[start] == 0:
            continue
        rewritten[start] = [(1, 0, addresses[start]), *rewritten[start]]
        explicit[start] = True
        addresses_written += 1

    def block_address(index):
        start = starts[index] if 0 <= index < len(starts) else None
        if start is None or not explicit[start]:
            return None
        return addresses[start]

    prepared = []
    edges_removed = graphs_removed = 0
    for graph in _messages(top, 7):
        members = [value for field, wire, value in graph if field == 1 and wire == 0]
        member_set = {index for index in members if block_address(index) is not None}
        if not member_set:
            graphs_removed += 1
            continue
        entry = _first_varint(graph, 3)
        entry = entry if entry in member_set else None
        if entry is None or block_address(entry) not in vertices:
            choices = [index for index in members if block_address(index) in vertices]
            if not choices:
                graphs_removed += 1
                continue
            entry = choices[0]
        kept = []
        for field, wire, value in graph:
            if field == 1 and wire == 0 and value not in member_set:
                continue
            if field == 2 and wire == 2:
                edge = _parse_fields(value)
                source = _first_varint(edge, 1) or 0
                target = _first_varint(edge, 2) or 0
                if source not in member_set or target not in member_set:
                    edges_removed += 1
                    continue
            if field == 3 and wire == 0:
                continue
            kept.append((field, wire, value))
        kept.append((3, 0, entry))
        prepared.append((block_address(entry), len(member_set), kept))

    selected = {}
    for address, size, graph in prepared:
        current = selected.get(address)
        if current is None or size > current[0]:
            if current is not None:
                graphs_removed += 1
            selected[address] = (size, graph)
        else:
            graphs_removed += 1

    instruction_iter = iter(rewritten)
    graph_iter = iter(graph for _, graph in selected.values())
    output = []
    for field, wire, value in top:
        if field == 5 and wire == 2:
            output.append((5, 2, _encode_fields(next(instruction_iter))))
        elif field == 7 and wire == 2:
            graph = next(graph_iter, None)
            if graph is not None:
                output.append((7, 2, _encode_fields(graph)))
        else:
            output.append((field, wire, value))
    return _encode_fields(output), {
        "block_addresses_written": addresses_written,
        "edges_removed": edges_removed,
        "flow_graphs_removed": graphs_removed,
    }
