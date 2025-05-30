{%- let obj = ci.get_object_definition(name).unwrap() %}
{%- let (protocol_name, impl_class_name) = obj|object_names %}
{%- let methods = obj.methods() %}
{%- let protocol_docstring = obj.docstring() %}

{%- let is_error = ci.is_name_used_as_error(name) %}

{% include "Protocol.swift" %}

{%- call swift::docstring(obj, 0) %}
open class {{ impl_class_name }}: {{ protocol_name }}, @unchecked Sendable {
    fileprivate let pointer: UnsafeMutableRawPointer!

    /// Used to instantiate a [FFIObject] without an actual pointer, for fakes in tests, mostly.
#if swift(>=5.8)
    @_documentation(visibility: private)
#endif
    public struct NoPointer {
        public init() {}
    }

    // TODO: We'd like this to be `private` but for Swifty reasons,
    // we can't implement `FfiConverter` without making this `required` and we can't
    // make it `required` without making it `public`.
#if swift(>=5.8)
    @_documentation(visibility: private)
#endif
    required public init(unsafeFromRawPointer pointer: UnsafeMutableRawPointer) {
        self.pointer = pointer
    }

    // This constructor can be used to instantiate a fake object.
    // - Parameter noPointer: Placeholder value so we can have a constructor separate from the default empty one that may be implemented for classes extending [FFIObject].
    //
    // - Warning:
    //     Any object instantiated with this constructor cannot be passed to an actual Rust-backed object. Since there isn't a backing [Pointer] the FFI lower functions will crash.
#if swift(>=5.8)
    @_documentation(visibility: private)
#endif
    public init(noPointer: NoPointer) {
        self.pointer = nil
    }

#if swift(>=5.8)
    @_documentation(visibility: private)
#endif
    public func uniffiClonePointer() -> UnsafeMutableRawPointer {
        return try! rustCall { {{ obj.ffi_object_clone().name() }}(self.pointer, $0) }
    }

    {%- match obj.primary_constructor() %}
    {%- when Some(cons) %}
    {%- call swift::ctor_decl(cons, 4) %}
    {%- when None %}
    // No primary constructor declared for this class.
    {%- endmatch %}

    deinit {
        guard let pointer = pointer else {
            return
        }

        try! rustCall { {{ obj.ffi_object_free().name() }}(pointer, $0) }
    }

    {% for cons in obj.alternate_constructors() %}
    {%- call swift::func_decl("public static func", cons, 4) %}
    {% endfor %}

    {% for meth in obj.methods() -%}
    {%- call swift::func_decl("open func", meth, 4) %}
    {% endfor %}

    {%- for tm in obj.uniffi_traits() %}
    {%-     match tm %}
    {%-         when UniffiTrait::Display { fmt } %}
    open var description: String {
        return {% call swift::is_try(fmt) %} {{ fmt.return_type().unwrap()|lift_fn }}(
            {% call swift::to_ffi_call(fmt) %}
        )
    }
    {%-         when UniffiTrait::Debug { fmt } %}
    open var debugDescription: String {
        return {% call swift::is_try(fmt) %} {{ fmt.return_type().unwrap()|lift_fn }}(
            {% call swift::to_ffi_call(fmt) %}
        )
    }
    {%-         when UniffiTrait::Eq { eq, ne } %}
    public static func == (self: {{ impl_class_name }}, other: {{ impl_class_name }}) -> Bool {
        return {% call swift::is_try(eq) %} {{ eq.return_type().unwrap()|lift_fn }}(
            {% call swift::to_ffi_call(eq) %}
        )
    }
    {%-         when UniffiTrait::Hash { hash } %}
    open func hash(into hasher: inout Hasher) {
        let val = {% call swift::is_try(hash) %} {{ hash.return_type().unwrap()|lift_fn }}(
            {% call swift::to_ffi_call(hash) %}
        )
        hasher.combine(val)
    }
    {%-         else %}
    {%-    endmatch %}
    {%- endfor %}

}

{%- if obj.has_callback_interface() %}
{%- let callback_handler = format!("uniffiCallbackInterface{}", name) %}
{%- let callback_init = format!("uniffiCallbackInit{}", name) %}
{%- let vtable = obj.vtable().expect("trait interface should have a vtable") %}
{%- let vtable_methods = obj.vtable_methods() %}
{%- let ffi_init_callback = obj.ffi_init_callback() %}
{% include "CallbackInterfaceImpl.swift" %}
{%- endif %}

{%- for tm in obj.uniffi_traits() %}
{%-     match tm %}
{%-         when UniffiTrait::Display { fmt } %}
extension {{ impl_class_name }}: CustomStringConvertible {}
{%-         when UniffiTrait::Debug { fmt } %}
extension {{ impl_class_name }}: CustomDebugStringConvertible {}
{%-         when UniffiTrait::Eq { eq, ne } %}
extension {{ impl_class_name }}: Equatable {}
{%-         when UniffiTrait::Hash { hash } %}
extension {{ impl_class_name }}: Hashable {}
{%-         else %}
{%-    endmatch %}
{%- endfor %}

{%- if is_error %}
extension {{ impl_class_name }}: Swift.Error {}
{% endif %}

{%- for t in obj.trait_impls() %}
extension {{impl_class_name}}: {{ self::trait_protocol_name(ci, t.trait_name)? }} {}
{% endfor %}


#if swift(>=5.8)
@_documentation(visibility: private)
#endif
public struct {{ ffi_converter_name }}: FfiConverter {
    {%- if obj.has_callback_interface() %}
    fileprivate static let handleMap = UniffiHandleMap<{{ type_name }}>()
    {%- endif %}

    typealias FfiType = UnsafeMutableRawPointer
    typealias SwiftType = {{ type_name }}

    public static func lift(_ pointer: UnsafeMutableRawPointer) throws -> {{ type_name }} {
        return {{ impl_class_name }}(unsafeFromRawPointer: pointer)
    }

    public static func lower(_ value: {{ type_name }}) -> UnsafeMutableRawPointer {
        {%- if obj.has_callback_interface() %}
        guard let ptr = UnsafeMutableRawPointer(bitPattern: UInt(truncatingIfNeeded: handleMap.insert(obj: value))) else {
            fatalError("Cast to UnsafeMutableRawPointer failed")
        }
        return ptr
        {%- else %}
        return value.uniffiClonePointer()
        {%- endif %}
    }

    public static func read(from buf: inout (data: Data, offset: Data.Index)) throws -> {{ type_name }} {
        let v: UInt64 = try readInt(&buf)
        // The Rust code won't compile if a pointer won't fit in a UInt64.
        // We have to go via `UInt` because that's the thing that's the size of a pointer.
        let ptr = UnsafeMutableRawPointer(bitPattern: UInt(truncatingIfNeeded: v))
        if (ptr == nil) {
            throw UniffiInternalError.unexpectedNullPointer
        }
        return try lift(ptr!)
    }

    public static func write(_ value: {{ type_name }}, into buf: inout [UInt8]) {
        // This fiddling is because `Int` is the thing that's the same size as a pointer.
        // The Rust code won't compile if a pointer won't fit in a `UInt64`.
        writeInt(&buf, UInt64(bitPattern: Int64(Int(bitPattern: lower(value)))))
    }
}

{#
We always write these public functions just in case the object is used as
an external type by another crate.
#}
#if swift(>=5.8)
@_documentation(visibility: private)
#endif
public func {{ ffi_converter_name }}_lift(_ pointer: UnsafeMutableRawPointer) throws -> {{ type_name }} {
    return try {{ ffi_converter_name }}.lift(pointer)
}

#if swift(>=5.8)
@_documentation(visibility: private)
#endif
public func {{ ffi_converter_name }}_lower(_ value: {{ type_name }}) -> UnsafeMutableRawPointer {
    return {{ ffi_converter_name }}.lower(value)
}

{# Objects as error #}
{%- if is_error %}

{% if !config.omit_localized_error_conformance() %}
extension {{ type_name }}: Foundation.LocalizedError {
    public var errorDescription: String? {
        String(reflecting: self)
    }
}
{% endif %}

{# Due to some mismatches in the ffi converter mechanisms, errors are a RustBuffer holding a pointer #}
#if swift(>=5.8)
@_documentation(visibility: private)
#endif
public struct {{ ffi_converter_name }}__as_error: FfiConverterRustBuffer {
    public static func lift(_ buf: RustBuffer) throws -> {{ type_name }} {
        var reader = createReader(data: Data(rustBuffer: buf))
        return try {{ ffi_converter_name }}.read(from: &reader)
    }

    public static func lower(_ value: {{ type_name }}) -> RustBuffer {
        fatalError("not implemented")
    }

    public static func read(from buf: inout (data: Data, offset: Data.Index)) throws -> {{ type_name }} {
        fatalError("not implemented")
    }

    public static func write(_ value: {{ type_name }}, into buf: inout [UInt8]) {
        fatalError("not implemented")
    }
}

{# Error FFI converters also need these public functions. #}
#if swift(>=5.8)
@_documentation(visibility: private)
#endif
public func {{ ffi_converter_name }}__as_error_lift(_ buf: RustBuffer) throws -> {{ type_name }} {
    return try {{ ffi_converter_name }}__as_error.lift(buf)
}

#if swift(>=5.8)
@_documentation(visibility: private)
#endif
public func {{ ffi_converter_name }}__as_error_lower(_ value: {{ type_name }}) -> RustBuffer {
    return {{ ffi_converter_name }}__as_error.lower(value)
}

{%- endif %}
