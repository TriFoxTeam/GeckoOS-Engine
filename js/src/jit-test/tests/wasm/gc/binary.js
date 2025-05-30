load(libdir + "wasm-binary.js");

function checkInvalid(body, errorMessage) {
    assertErrorMessage(() => new WebAssembly.Module(
        moduleWithSections([
            typeSection([
                { kind: FuncCode, args: [], ret: [] },
            ]),
            declSection([0]),
            bodySection([body]),
        ])
    ), WebAssembly.CompileError, errorMessage);
}

const invalidRefBlockType = funcBody({locals:[], body:[
    BlockCode,
    OptRefCode,
    0x42,
    EndCode,
]});
checkInvalid(invalidRefBlockType, /heap type/);

const invalidTooBigRefType = funcBody({locals:[], body:[
    BlockCode,
    OptRefCode,
    ...varU32(1000000),
    EndCode,
]});
checkInvalid(invalidTooBigRefType, /heap type/);
