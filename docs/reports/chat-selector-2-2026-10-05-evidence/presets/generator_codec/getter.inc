inline const Json& builtin_generator_codec_fixture() {
  static const Json value = Json::parse("{\"dictionary\":{\")W3_PRESET\\\"\":\"combined )W3_PRESET\\\"\\\\\\n\\u0000🙂ż\",\"NUL\\u0000inside\":\"Zażółć gęślą jaźń\"},\"metadata\":{\"area\":\")W3_PRESET\\\"\",\"id\":\"combined )W3_PRESET\\\"\\\\\\n\\u0000🙂ż\",\"key\":\"NUL\\u0000inside\"},\"strings\":[\")W3_PRESET\\\"\",\"\\\"quoted\\\"\",\"backslash\\\\path\",\"line one\\nline two\",\"NUL\\u0000inside\",\"emoji🙂\",\"Zażółć gęślą jaźń\",\"literal \\\\u0000\",\"CRLF\\r\\n\",\"trailing\\\\\",\"tab\\t\",\"separator \",\"combined )W3_PRESET\\\"\\\\\\n\\u0000🙂ż\"]}");
  return value;
}
