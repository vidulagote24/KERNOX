#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/experiment/experiment_runner.hpp"

#include <string>
#include <string_view>

namespace kernox::experiment {

// Lightweight, zero-dependency JSON and CSV serializer for experiment results and comparisons.
// Produces schemas directly consumable by external data analytics and visualization tools.
class ExperimentExporter {
public:
    // Serializes an individual policy experiment result into structured JSON.
    [[nodiscard]] static std::string to_json(const ExperimentResult& result);

    // Serializes a multi-policy comparison benchmark into structured JSON.
    [[nodiscard]] static std::string to_json(const ExperimentComparison& comparison);

    // Serializes a multi-policy comparison benchmark summary into tabular CSV.
    [[nodiscard]] static std::string to_csv(const ExperimentComparison& comparison);

    // Serializes individual reclamation decisions from an experiment result into CSV.
    [[nodiscard]] static std::string to_decisions_csv(const ExperimentResult& result);

    // Escapes special characters (\", \\, \n, \r, \t, etc.) for JSON RFC 8259 compliance.
    [[nodiscard]] static std::string escape_json_string(std::string_view input);

    // Writes string content to an output file path.
    static core::Result<void> write_to_file(const std::string& path, std::string_view content);
};

} // namespace kernox::experiment
