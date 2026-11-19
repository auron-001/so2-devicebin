#pragma once

#include <cstdint>

// Единственный файл со всеми оффсетами.
// Всё, что завязано на версию игры / layout рантайма, лежит только здесь.
namespace ofset {

// --- версия игры ---
constexpr char module_name[] = "libunity.so";
constexpr char game_version[] = "1.1.0";
constexpr std::uint32_t build = 203982;
constexpr std::int32_t environment_version = 1101;

// --- TypeInfo слоты: RVA от базы libunity.so (dump 1.1.0) ---
namespace typeinfo {
constexpr std::uint64_t bolt_player_api = 0x0CD1F068;
constexpr std::uint64_t facebook_auth_request = 0x0CD21C40;
constexpr std::uint64_t verification = 0x0CD2C620;
constexpr std::uint64_t app_verification = 0x0CD1E050;
constexpr std::uint64_t app_check_tokens = 0x0CD1DFB0;
} // namespace typeinfo

// --- имена классов, которые ищем/сверяем ---
namespace class_name {
constexpr char bolt_player_api[] = "BoltPlayerApi";
constexpr char bolt_player_api_namespace[] = "Axlebolt.Bolt";
constexpr char bolt_player_api_full[] = "Axlebolt.Bolt.BoltPlayerApi";
constexpr char verification[] = "Verification";
constexpr char verification_full[] = "Axlebolt.Bolt.Verification";
constexpr char app_verification[] = "AppVerification";
constexpr char app_verification_namespace[] = "Axlebolt.Bolt.Protobuf";
constexpr char app_verification_full[] = "Axlebolt.Bolt.Protobuf.AppVerification";
constexpr char app_check_tokens[] = "AppCheckTokens";
constexpr char facebook_auth_request[] = "FacebookAuthRequest";
constexpr char facebook_auth_request_namespace[] = "Axlebolt.Bolt.Protobuf";
constexpr char auth_facebook_full[] = "Axlebolt.Bolt.Protobuf.AuthFacebook";
constexpr char facebook_credential_full[] = "Axlebolt.Bolt.Auth.FacebookCredential";
constexpr char device_info_full[] = "Axlebolt.Bolt.DeviceInfo";
constexpr char device_info_proto_full[] = "Axlebolt.Bolt.Protobuf.DeviceInfo";
} // namespace class_name

// --- Il2CppClass (LP64, sizeof = head_size) ---
namespace il2cpp_class {
constexpr std::uint64_t image = 0x00;
constexpr std::uint64_t type_metadata_handle = 0x50;
constexpr std::uint64_t namespaze = 0x90;
constexpr std::uint64_t instance_size = 0x98;
constexpr std::uint64_t method_count = 0xB2;
constexpr std::uint64_t interface_count = 0xC8;
constexpr std::uint64_t methods = 0xD0;
constexpr std::uint64_t flags = 0xDC;
constexpr std::uint64_t fields = 0xE0;
constexpr std::uint64_t static_fields = 0xF8;
constexpr std::uint64_t field_count = 0x100;
constexpr std::uint64_t declaring_type = 0x110;
constexpr std::uint64_t parent = 0x148;
constexpr std::uint64_t implemented_interfaces = 0x150;
constexpr std::uint64_t name = 0x188;
constexpr std::uint64_t generic_class = 0x1A0;
constexpr std::uint64_t head_size = 0x1B0;
constexpr std::uint64_t element_class = 0x10;
constexpr std::uint64_t static_fields_dump_size = 0x40;
} // namespace il2cpp_class

// --- FieldInfo ---
namespace il2cpp_field {
constexpr std::uint64_t name = 0x00;
constexpr std::uint64_t token = 0x08;
constexpr std::uint64_t parent = 0x10;
constexpr std::uint64_t type = 0x18;
constexpr std::uint64_t offset = 0x20;
constexpr std::uint64_t stride = 0x30;
constexpr std::uint64_t name_candidates[] = {0x00, 0x08, 0x10, 0x18, 0x20};
constexpr std::uint64_t offset_candidates[] = {0x18, 0x1C, 0x20, 0x24,
                                               0x28, 0x2C, 0x30, 0x34, 0x38};
constexpr std::uint64_t stride_candidates[] = {0x20, 0x28, 0x30, 0x38, 0x40};
} // namespace il2cpp_field

// --- MethodInfo ---
namespace il2cpp_method {
constexpr std::uint64_t name = 0x30;
constexpr std::uint64_t token = 0x1C;
constexpr std::uint64_t params_count = 0x38;
constexpr std::uint64_t method_pointer = 0x68;
constexpr std::uint64_t size = 0x70;
} // namespace il2cpp_method

// --- Il2CppType (byval_arg) ---
namespace il2cpp_type {
constexpr std::uint64_t data = 0x00;
constexpr std::uint64_t type_enum = 0x0A;
} // namespace il2cpp_type

// --- любой managed-объект ---
namespace object {
constexpr std::uint64_t klass = 0x00;
constexpr std::uint64_t monitor = 0x08;
constexpr std::uint64_t first_field = 0x10;
} // namespace object

// --- System.String ---
namespace string_object {
constexpr std::uint64_t length = 0x10;
constexpr std::uint64_t chars = 0x14;
} // namespace string_object

// --- System.Array / T[] / byte[] ---
namespace array_object {
constexpr std::uint64_t length = 0x18;
constexpr std::uint64_t data = 0x20;
} // namespace array_object

// --- Google.Protobuf.ByteString ---
namespace byte_string {
constexpr std::uint64_t data = 0x10;
constexpr std::uint64_t index = 0x18;
constexpr std::uint64_t length = 0x1C;
} // namespace byte_string

// --- Google.Protobuf.Collections.RepeatedField<T> ---
namespace repeated_field {
constexpr std::uint64_t array_candidates[] = {0x10, 0x18, 0x20};
constexpr std::uint64_t count_delta = 0x08;
} // namespace repeated_field

// --- Google.Protobuf.Collections.MapField<TKey, TValue> ---
namespace map_field {
constexpr std::uint64_t list_candidates[] = {0x10, 0x18, 0x20};
} // namespace map_field

// --- System.Collections.Generic.LinkedList<T> (внутри MapField) ---
namespace linked_list {
constexpr std::uint64_t head = 0x10;
constexpr std::uint64_t count = 0x18;
constexpr std::uint64_t node_next = 0x18;
constexpr std::uint64_t node_key = 0x28;
constexpr std::uint64_t node_value = 0x30;
} // namespace linked_list

// --- System.Collections.Generic.Dictionary<TKey, TValue> ---
namespace dictionary {
constexpr std::uint64_t entries = 0x18;
constexpr std::uint64_t count = 0x20;
constexpr std::uint64_t entry_hash = 0x00;
constexpr std::uint64_t entry_key = 0x08;
constexpr std::uint64_t entry_value = 0x10;
constexpr std::uint64_t entry_strides[] = {0x18, 0x20};
} // namespace dictionary

// --- делегаты / MessageParser ---
namespace delegate_object {
constexpr std::uint64_t method_ptr = 0x10;
constexpr std::uint64_t target = 0x20;
} // namespace delegate_object

namespace message_parser {
constexpr std::uint64_t factory = 0x10;
} // namespace message_parser

// --- Axlebolt.Bolt.BoltPlayerApi ---
namespace bolt_static_fields {
constexpr std::uint64_t instance = 0x00;
} // namespace bolt_static_fields

namespace bolt_player_api {
constexpr std::uint64_t game_id = 0x40;
constexpr std::uint64_t game_version = 0x48;
constexpr std::uint64_t credential = 0xB0;
constexpr std::uint64_t device_info = 0xB8;
constexpr std::uint64_t verification = 0xC0;
constexpr std::uint64_t state = 0xC8;
} // namespace bolt_player_api

namespace player_api_state {
constexpr std::uint64_t authenticated = 0x10;
} // namespace player_api_state

// --- Axlebolt.Bolt.Verification (retained object) ---
namespace retained_verification {
constexpr std::uint64_t apk_hash = 0x10;
constexpr std::uint64_t app_snapshot = 0x18;
constexpr std::uint64_t content_hash = 0x20;
constexpr std::uint64_t is_rooted = 0x28;
constexpr std::uint64_t forbidden_apps = 0x30;
constexpr std::uint64_t path = 0x38;
constexpr std::uint64_t environment = 0x40;
} // namespace retained_verification

// --- Environment (внутри Verification) ---
namespace environment {
constexpr std::uint64_t byte_array = 0x10;
constexpr std::uint64_t version = 0x18;
} // namespace environment

// --- Axlebolt.Bolt.Protobuf.FacebookAuthRequest ---
namespace facebook_auth_request {
constexpr std::uint64_t auth_facebook = 0x18;
constexpr std::uint64_t app_verification = 0x20;
constexpr std::uint64_t device_info = 0x28;
} // namespace facebook_auth_request

// --- Axlebolt.Bolt.Protobuf.AuthFacebook ---
namespace auth_facebook {
constexpr std::uint64_t game_id = 0x18;
constexpr std::uint64_t game_version = 0x20;
constexpr std::uint64_t platform = 0x28;
constexpr std::uint64_t token = 0x30;
constexpr std::uint64_t locale = 0x38;
constexpr std::uint64_t store = 0x40;
} // namespace auth_facebook

// --- Axlebolt.Bolt.Auth.FacebookCredential ---
namespace facebook_credential {
constexpr std::uint64_t token = 0x10;
} // namespace facebook_credential

// --- Axlebolt.Bolt.Protobuf.AppVerification ---
namespace app_verification {
constexpr std::uint64_t is_rooted = 0x18;
constexpr std::uint64_t apk_hash = 0x20;
constexpr std::uint64_t forbidden_apps = 0x28;
constexpr std::uint64_t path = 0x30;
constexpr std::uint64_t content_hash = 0x38;
constexpr std::uint64_t app_snapshot = 0x40;
constexpr std::uint64_t environment = 0x58;
constexpr std::uint64_t token = 0x60;
} // namespace app_verification

// --- Environment (protobuf-версия внутри AppVerification) ---
namespace app_verification_environment {
constexpr std::uint64_t version = 0x18;
constexpr std::uint64_t info = 0x20;
} // namespace app_verification_environment

// --- Axlebolt.Bolt.AppCheckTokens ---
namespace app_check_tokens {
constexpr std::uint64_t payload = 0x18;
} // namespace app_check_tokens

// --- Axlebolt.Bolt.DeviceInfo (managed) ---
namespace device_info {
constexpr std::uint64_t device_id = 0x10;
constexpr std::uint64_t device_model = 0x18;
constexpr std::uint64_t ads_id = 0x20;
constexpr std::uint64_t vndr_id = 0x28;
} // namespace device_info

// --- Axlebolt.Bolt.Protobuf.DeviceInfo ---
namespace device_info_proto {
constexpr std::uint64_t device_id = 0x18;
constexpr std::uint64_t device_model = 0x20;
constexpr std::uint64_t ads_id = 0x28;
constexpr std::uint64_t vndr_id = 0x30;
} // namespace device_info_proto

} // namespace ofset
