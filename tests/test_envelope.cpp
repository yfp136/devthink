// 信封解析/构造单元测试（规格 §5.1）
#include <string>

#include "core/envelope.h"
#include "core/error_codes.h"
#include "test_common.h"

namespace {

using sm::Envelope;
using sm::make_cmd;
using sm::make_event;
using sm::make_reply;
using sm::parse_envelope;
using nlohmann::json;

void test_roundtrip_valid_cmd() {
  const Envelope cmd = make_cmd("engine.ui", "engine.media", "media.play",
                                json{{"media_id", "m-abc"}});
  Envelope out;
  const auto r = parse_envelope(sm::envelope_to_json(cmd), out);
  SM_CHECK(r.ok);
  SM_CHECK(out.id == cmd.id);
  SM_CHECK(out.src == "engine.ui" && out.dst == "engine.media");
  SM_CHECK(out.type == "cmd" && out.op == "media.play");
  SM_CHECK(out.monotonic_ms == cmd.monotonic_ms);
  SM_CHECK(out.params.at("media_id") == "m-abc");
}

void test_make_reply_semantics() {
  const Envelope cmd = make_cmd("engine.ui", "engine.media", "media.play");
  const Envelope ok = make_reply(cmd, sm::ec::OK);
  SM_CHECK(ok.type == "rsp");
  SM_CHECK(ok.code == 0 && ok.has_code);
  SM_CHECK(ok.src == "engine.media" && ok.dst == "engine.ui");  // 方向反转
  SM_CHECK(ok.ref_id == cmd.id);
  SM_CHECK(ok.op == cmd.op);

  const Envelope err = make_reply(cmd, sm::ec::BAD_PARAM);
  SM_CHECK(err.type == "err");
  SM_CHECK(err.code == sm::ec::BAD_PARAM);
}

void test_serialized_reply_carries_code() {
  const Envelope cmd = make_cmd("engine.ui", "engine.media", "sys.ping");
  const Envelope err = make_reply(cmd, sm::ec::UNKNOWN_OP);
  Envelope out;
  SM_CHECK(parse_envelope(sm::envelope_to_json(err), out).ok);
  SM_CHECK(out.code == sm::ec::UNKNOWN_OP && out.type == "err");
}

void test_invalid_inputs() {
  Envelope out;
  SM_CHECK(!parse_envelope("{not json", out).ok);
  SM_CHECK(!parse_envelope("[]", out).ok);

  const std::string bad_v =
      R"({"v":2,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping"})";
  SM_CHECK(!parse_envelope(bad_v, out).ok);

  const std::string no_id =
      R"({"v":1,"monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping"})";
  SM_CHECK(!parse_envelope(no_id, out).ok);

  const std::string cmd_code =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping","code":0})";
  SM_CHECK(!parse_envelope(cmd_code, out).ok);

  const std::string rsp_nocode =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"rsp","op":"sys.ping"})";
  SM_CHECK(!parse_envelope(rsp_nocode, out).ok);

  const std::string bad_type =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"ping","op":"sys.ping"})";
  SM_CHECK(!parse_envelope(bad_type, out).ok);

  const std::string arr_params =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping","params":[1]})";
  SM_CHECK(!parse_envelope(arr_params, out).ok);
}

void test_evt_no_code_ok() {
  const Envelope evt = make_event("engine.media", "evt.engine.heartbeat");
  SM_CHECK(evt.dst == "*");
  Envelope out;
  SM_CHECK(parse_envelope(sm::envelope_to_json(evt), out).ok);
  SM_CHECK(!out.has_code);  // 事件不得携带 code
}

}  // namespace

int main() {
  test_roundtrip_valid_cmd();
  test_make_reply_semantics();
  test_serialized_reply_carries_code();
  test_invalid_inputs();
  test_evt_no_code_ok();
  return smtest::finish("test_envelope");
}
