#include "sns/rendering/smp_xml.hpp"

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <xmllite.h>

#include "sns/core/text.hpp"
#endif

namespace sns::rendering {

SmpXmlReport parse_smp_xml(std::string_view xml) {
  SmpXmlReport report;
  if (xml.empty() || xml.size() > 4u * 1024u * 1024u) {
    report.error = "XML empty or exceeds 4 MiB limit";
    return report;
  }
#ifdef _WIN32
  using Microsoft::WRL::ComPtr;
  ComPtr<IStream> stream;
  stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()), static_cast<UINT>(xml.size())));
  if (!stream) {
    report.error = "XML memory stream unavailable";
    return report;
  }
  ComPtr<IXmlReader> reader;
  if (FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr)) ||
      FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
      FAILED(reader->SetInput(stream.Get()))) {
    report.error = "XML reader unavailable";
    return report;
  }

  XmlNodeType type;
  HRESULT hr = S_OK;
  while ((hr = reader->Read(&type)) == S_OK) {
    if (type != XmlNodeType_Element) continue;
    const WCHAR* local = nullptr;
    UINT local_len = 0;
    if (FAILED(reader->GetLocalName(&local, &local_len)) || !local) continue;
    const std::wstring_view element(local, local_len);
    const bool shape = element == L"per-vertex-shape" || element == L"per-triangle-shape";
    const bool bone = element == L"bone";
    if (!shape && !bone) continue;

    for (HRESULT attr = reader->MoveToFirstAttribute(); attr == S_OK; attr = reader->MoveToNextAttribute()) {
      const WCHAR* attr_name = nullptr;
      UINT attr_len = 0;
      if (FAILED(reader->GetLocalName(&attr_name, &attr_len)) ||
          std::wstring_view(attr_name, attr_len) != L"name") continue;
      const WCHAR* value = nullptr;
      UINT value_len = 0;
      if (FAILED(reader->GetValue(&value, &value_len)) || !value) continue;
      auto name = text::to_utf8(std::wstring_view(value, value_len));
      auto& list = shape ? report.collision_shapes : report.bones;
      if (!name.empty() && std::find(list.begin(), list.end(), name) == list.end()) list.push_back(std::move(name));
      break;
    }
    reader->MoveToElement();
  }
  if (hr != S_FALSE) {
    report.collision_shapes.clear();
    report.bones.clear();
    report.error = "XML malformed, prohibited DTD, or unsupported encoding";
    return report;
  }
  report.well_formed = true;
#else
  report.error = "SMP XML parser unavailable on this platform";
#endif
  return report;
}

}  // namespace sns::rendering
