#pragma once

#include <AutoRegisterDB.hpp>
#include <BaseID.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class DocumentManager;

struct DBCopyResult {
    bool success = false;
    std::vector<DBInstanceID> rootIds;
    std::unordered_map<DBInstanceID, DBInstanceID> idMap;
    std::string error;
};

/** Copies a document ownership subgraph inside the caller's transaction. */
class DBCopyService {
public:
    explicit DBCopyService(DocumentManager& document);

    DBCopyResult copy(const std::vector<DBInstanceID>& sourceRootIds) const;

private:
    DocumentManager& m_document;
};
