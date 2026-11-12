#include "ExternalOpenRequest.hpp"
#include <QCoreApplication>
#include <QUrlQuery>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    try {
        ExternalOpenRequest request;
        QString error;
        const QString source = "https://example.com/model%20name.stl?signature=a%2Bb&other=1";
        QUrl link("gplatform://open"); QUrlQuery query;
        query.addQueryItem("file", QString::fromLatin1(QUrl::toPercentEncoding(source)));
        link.setQuery(query);
        require(parseExternalUrl(link, request, error), "valid encoded URL rejected");
        require(request.downloadUrl.toString(QUrl::FullyEncoded) == source, "signature or encoding changed");
        require(!parseExternalUrl(QUrl("gplatform://open?file=https%3A%2F%2Fexample.com%2Fa.stl&file=x"), request, error), "duplicate accepted");
        require(!parseExternalUrl(QUrl("gplatform://open?file=http%3A%2F%2Fexample.com%2Fa.stl"), request, error), "HTTP accepted");
        require(!parseExternalUrl(QUrl("gplatform://open?file=https%3A%2F%2Fexample.com%2Fa.stl&name=..%2Fa.stl"), request, error), "path filename accepted");
        require(!parseExternalUrl(QUrl("gplatform://open?file=https%3A%2F%2Fexample.com%2Fa.stl&name=a.3mf"), request, error), "conflicting extension accepted");
        require(!parseExternalUrl(QUrl("gplatform://open?file=https%3A%2F%2Fexample.com%2Fa.obj&name=a.stl"), request, error), "unsupported URL format disguised by name");
        require(parseExternalUrl(QUrl("gplatform://open?file=https%3A%2F%2Fexample.com%2F123&name=model.3mf"), request, error), "opaque endpoint rejected");
        auto parsed = parseStartupInputs({"app", "中文 model.stl", "project.3mf", "--restart"}, "/tmp");
        require(parsed.errors.isEmpty() && parsed.requests.size() == 2 && parsed.restartArguments == QStringList{"--restart"}, "one-shot inputs leaked to restart");
        require(parsed.requests[0].localPath == "/tmp/中文 model.stl", "relative path lost sender cwd");
        const auto mixed = parseStartupInputs({"app", "one.stl", "bad.obj", "two.stl"}, "/tmp");
        require(mixed.errors.isEmpty() && mixed.requests.size() == 3 &&
            !mixed.requests[1].inputError.isEmpty() && mixed.requests[2].inputError.isEmpty(),
            "invalid file swallowed valid batch members");
        const auto empty = parseStartupInputs({"app", "", "two.stl"}, "/tmp");
        require(empty.errors.isEmpty() && empty.requests.size() == 2 && !empty.requests[0].inputError.isEmpty(), "empty input swallowed valid file");
        auto dedicated = parseStartupInputs({"app", "--ghost-port", "bad", "--journal-replay-id", "owned"}, "/tmp");
        require(dedicated.dedicatedJournal && dedicated.errors.isEmpty() && dedicated.restartArguments.contains("bad"), "dedicated port diagnostic lost");
        require(!parseStartupInputs({"app", "--unknown", "model.stl"}, "/tmp").errors.isEmpty(), "unknown option ignored");
        std::cout << "PASS external inputs, URL encoding, filename policy, dedicated mode and restart filtering\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
