// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "RawFrameDataWriter.h"
#include <Core/source/infra/util/Util.h>
#include <Core/source/infra/util/Assert.h>
#include <CommonUtilities//str/String.h>
#include <PresentMonAPIWrapper/FrameQuery.h>
#include <PresentMonAPIWrapperCommon/EnumMap.h>
#include <format>
#include <array>
#include "RawFrameDataMetricList.h"

namespace p2c::pmon
{
    using ::pmon::util::str::ToNarrow;
    namespace rn = std::ranges;
    namespace vi = rn::views;

    namespace
    {
        // type to activate special templatate specialization for time
        struct TimeAnnotationType_{};

        struct Annotation_
        {
            virtual ~Annotation_() = default;
            virtual void Write(std::ostream& out, const uint8_t* pBytes) const = 0;
            std::string columnName;
            static std::unique_ptr<Annotation_> MakeTyped(PM_METRIC metricId, uint32_t deviceId,
                const pmapi::intro::MetricView& metric);
        };
        template<typename T>
        struct TypedAnnotation_ : public Annotation_
        {
        public:
            TypedAnnotation_(bool zeroForNA = false) : zeroForNA_{ zeroForNA } {}
            void Write(std::ostream& out, const uint8_t* pBytes) const override
            {
                if constexpr (std::same_as<T, const char*>) {
                    out << reinterpret_cast<T>(pBytes);
                }
                else if constexpr (std::floating_point<T>) {
                    const auto val = *reinterpret_cast<const T*>(pBytes);
                    if (std::isnan(val)) {
                        out << (zeroForNA_ ? "0" : "NA");
                    }
                    else {
                        out << val;
                    }
                }
                else {
                    out << *reinterpret_cast<const T*>(pBytes);
                }
            }
        private:
            bool zeroForNA_;
        };
        template<>
        struct TypedAnnotation_<void> : public Annotation_
        {
            void Write(std::ostream& out, const uint8_t* pBytes) const override
            {
                out << "NA";
            }
        };
        template<>
        struct TypedAnnotation_<PM_ENUM> : public Annotation_
        {
            TypedAnnotation_(PM_ENUM enumId) : pKeyMap{ pmapi::EnumMap::GetKeyMap(enumId) } {}
            void Write(std::ostream& out, const uint8_t* pBytes) const override
            {
                out << pKeyMap->at(*reinterpret_cast<const int*>(pBytes)).narrowName;
            }
            std::shared_ptr<const pmapi::EnumMap::KeyMap> pKeyMap;
        };
        template<>
        struct TypedAnnotation_<TimeAnnotationType_> : public Annotation_
        {
            void Write(std::ostream& out, const uint8_t* pBytes) const override
            {
                if (startTime) {
                    out << (*reinterpret_cast<const double*>(pBytes) - *startTime) * 0.001;
                }
                else {
                    startTime = *reinterpret_cast<const double*>(pBytes);
                    out << 0.;
                }
            }
            mutable std::optional<double> startTime;
        };
        std::unique_ptr<Annotation_> Annotation_::MakeTyped(PM_METRIC metricId, uint32_t deviceId,
            const pmapi::intro::MetricView& metric)
        {
            // set availability (defaults to false, if we find matching device use its availability)
            bool available = false;
            for (auto di : metric.GetDeviceMetricInfo()) {
                if (di.GetDevice().GetId() != deviceId) continue;
                available = di.IsAvailable();
            }
            std::unique_ptr<Annotation_> pAnnotation;
            if (available) {
                const auto typeId = metric.GetDataTypeInfo().GetFrameType();
                // DISPLAYED_TIME and DISPLAY_LATENCY should just show zeros when NaN is encountered
                const bool zeroForNA = (metricId == PM_METRIC_DISPLAYED_TIME)
                    || (metricId == PM_METRIC_DISPLAY_LATENCY);
                // special case for TIME, it needs to be relative to TIME of first frame and scaled ms => s
                if (metricId == PM_METRIC_TIME) {
                    pAnnotation = std::make_unique<TypedAnnotation_<TimeAnnotationType_>>();
                }
                else {
                    switch (typeId) {
                    case PM_DATA_TYPE_BOOL: pAnnotation = std::make_unique<TypedAnnotation_<bool>>(); break;
                    case PM_DATA_TYPE_INT32: pAnnotation = std::make_unique<TypedAnnotation_<int32_t>>(); break;
                    case PM_DATA_TYPE_UINT32: pAnnotation = std::make_unique<TypedAnnotation_<uint32_t>>(); break;
                    case PM_DATA_TYPE_UINT64: pAnnotation = std::make_unique<TypedAnnotation_<uint64_t>>(); break;
                    case PM_DATA_TYPE_DOUBLE: pAnnotation = std::make_unique<TypedAnnotation_<double>>(zeroForNA); break;
                    case PM_DATA_TYPE_STRING: pAnnotation = std::make_unique<TypedAnnotation_<const char*>>(); break;
                    case PM_DATA_TYPE_ENUM: pAnnotation = std::make_unique<TypedAnnotation_<PM_ENUM>>(
                        metric.GetDataTypeInfo().GetEnumId()); break;
                    default: pAnnotation = std::make_unique<TypedAnnotation_<void>>(); break;
                    }
                }
            }
            else {
                pAnnotation = std::make_unique<TypedAnnotation_<void>>();
            }
            // set the column name for the element
            // remove spaces from metric name by range filter
            pAnnotation->columnName = metric.Introspect().GetName() |
                vi::filter([](char c) { return c != ' '; }) |
                rn::to<std::basic_string>();
            return pAnnotation;
        }
    }

    class QueryElementContainer_
    {
    public:
        QueryElementContainer_(std::span<const RawFrameQueryElementDefinition> elements,
            pmapi::Session& session, const pmapi::intro::Root& introRoot)
        {
            for (auto& el : elements) {
                const auto metric = introRoot.FindMetric(el.metricId);
                annotationPtrs_.push_back(Annotation_::MakeTyped(el.metricId, el.deviceId, metric));          
                // append index if array metric
                if (el.index.has_value()) {
                    annotationPtrs_.back()->columnName += std::format("[{}]", *el.index);
                }
                queryElements_.push_back(PM_QUERY_ELEMENT{
                    .metric = el.metricId,
                    .stat = PM_STAT_NONE,
                    .deviceId = el.deviceId,
                    .arrayIndex = el.index.value_or(0),
                });
                // check if metric is one of the specially-required fields
                // these fields are required because they are used for summary stats
                // we need pointers to these specific ones to read for generating those stats
                if (el.metricId == PM_METRIC_TIME) {
                    totalTimeElementIdx_ = int(queryElements_.size() - 1);
                }
                else if (el.metricId == PM_METRIC_FRAME_TIME) {
                    frametimeElementIdx_ = int(queryElements_.size() - 1);
                }
            }
            // if any specially-required fields are missing, add to query (but not to annotations)
            if (totalTimeElementIdx_ < 0) {
                queryElements_.push_back(PM_QUERY_ELEMENT{
                    .metric = PM_METRIC_TIME,
                    .stat = PM_STAT_NONE,
                    .deviceId = 0,
                    .arrayIndex = 0,
                });
                totalTimeElementIdx_ = int(queryElements_.size() - 1);
            }
            if (frametimeElementIdx_ < 0) {
                queryElements_.push_back(PM_QUERY_ELEMENT{
                    .metric = PM_METRIC_FRAME_TIME,
                    .stat = PM_STAT_NONE,
                    .deviceId = 0,
                    .arrayIndex = 0,
                });
                frametimeElementIdx_ = int(queryElements_.size() - 1);
            }
            // register query
            query_ = session.RegisterFrameQuery(queryElements_);
        }
        pmapi::BlobContainer MakeBlobs(uint32_t nBlobsToCreate) const
        {
            return query_.MakeBlobContainer(nBlobsToCreate);
        }
        void Consume(const pmapi::ProcessTracker& proc, pmapi::BlobContainer& blobs)
        {
            query_.Consume(proc, blobs);
        }
        double ExtractTotalTimeFromBlob(const uint8_t* pBlob) const
        {
            return reinterpret_cast<const double&>(pBlob[queryElements_[totalTimeElementIdx_].dataOffset]);
        }
        double ExtractFrameTimeFromBlob(const uint8_t* pBlob) const
        {
            return reinterpret_cast<const double&>(pBlob[queryElements_[frametimeElementIdx_].dataOffset]);
        }
        void WriteFrame(uint32_t pid, const std::string& procName, std::ostream& out, const uint8_t* pBlob)
        {
            // TODO: use metrics from procname and pid
            // process details are hardcoded here
            out << procName << ',' << pid;
            // loop over each element (column/field) in a frame of data
            for (auto&& [pAnno, query] : std::views::zip(annotationPtrs_, queryElements_)) {
                out << ',';
                // using output from the query registration of get offset of column's data
                const auto pBytes = pBlob + query.dataOffset;
                // annotation contains polymorphic info to reinterpret and convert bytes
                pAnno->Write(out, pBytes);
            }
            out << "\n";
        }
        void WriteHeader(std::ostream& out)
        {
            out << "Application,ProcessID";
            for (const auto& pAnno : annotationPtrs_) {
                out << ',' << pAnno->columnName;
            }
            out << std::endl;
        }
    private:
        pmapi::FrameQuery query_;
        // annotations encode logic for interpreting, post-processing, and formatting blob data for a query element
        std::vector<std::unique_ptr<Annotation_>> annotationPtrs_;
        // all query elements to be registered with the query, maintained to store blob offset information
        std::vector<PM_QUERY_ELEMENT> queryElements_;
        // query elements referenced used for summary stats gathering
        int totalTimeElementIdx_ = -1;
        int frametimeElementIdx_ = -1;
    };

    RawFrameDataWriter::RawFrameDataWriter(std::wstring path, const pmapi::ProcessTracker& procTrackerIn, std::wstring processName, uint32_t activeDeviceId,
        pmapi::Session& session, std::optional<std::wstring> frameStatsPathIn, const pmapi::intro::Root& introRoot)
        :
        procTracker{ procTrackerIn },
        procName{ ToNarrow(processName) },
        frameStatsPath{ std::move(frameStatsPathIn) },
        pStatsTracker{ frameStatsPath ? std::make_unique<StatisticsTracker>() : nullptr },
        file{ path }
    {
        auto queryElements = GetRawFrameDataMetricList(activeDeviceId);
        pQueryElementContainer = std::make_unique<QueryElementContainer_>(queryElements, session, introRoot);
        blobs = pQueryElementContainer->MakeBlobs(numberOfBlobs);
                
        // write header
        pQueryElementContainer->WriteHeader(file);
    }

    void RawFrameDataWriter::Process()
    {
        // continue consuming frames until none are left pending
        do {
            pQueryElementContainer->Consume(procTracker, blobs);
            // loop over populated blobs
            for (auto pBlob : blobs) {
                if (pStatsTracker) {
                    // tracking trace duration
                    if (startTime < 0.) {
                        startTime = pQueryElementContainer->ExtractTotalTimeFromBlob(pBlob);
                        endTime = startTime;
                    }
                    else {
                        endTime = pQueryElementContainer->ExtractTotalTimeFromBlob(pBlob);
                    }
                    // tracking frame times
                    pStatsTracker->Push(pQueryElementContainer->ExtractFrameTimeFromBlob(pBlob));
                }
                pQueryElementContainer->WriteFrame(procTracker.GetPid(), procName, file, pBlob);
            }
        } while (blobs.AllBlobsPopulated()); // if container filled, means more might be left
        file << std::flush;
    }

    double RawFrameDataWriter::GetDuration_() const
    {
        return (endTime - startTime) / 1000.;
    }

    void RawFrameDataWriter::WriteStats_()
    {
        auto& stats = *pStatsTracker;

        std::ofstream statsFile{ *frameStatsPath, std::ios::trunc };

        // write header
        statsFile <<
            "Duration,"
            "Total Frames,"
            "Average FPS,"
            "Minimum FPS,"
            "99th Percentile FPS,"
            "95th Percentile FPS,"
            "Maximum FPS\n";

        // lambda to make sure we don't divide by zero
        // caps max fps output to 1,000,000 fps
        const auto SafeInvert = [](double ft) {
            return ft == 0. ? 1'000'000. : 1. / ft;
		};

        if (stats.GetCount() > 0) {
            // write data
            statsFile <<
                GetDuration_() << "," <<
                stats.GetCount() << "," <<
                SafeInvert(stats.GetMean()) << "," <<
                SafeInvert(stats.GetMax()) << "," <<
                SafeInvert(stats.GetPercentile(.99)) << "," <<
                SafeInvert(stats.GetPercentile(.95)) << "," <<
                SafeInvert(stats.GetMin()) << "\n";
        }
        else {
			// write null data
			statsFile <<
				0. << "," <<
				0. << "," <<
				0. << "," <<
				0. << "," <<
				0. << "," <<
				0. << "," <<
				0. << "\n";
        }
    }

    RawFrameDataWriter::~RawFrameDataWriter()
    {
        try {
            if (pStatsTracker) {
                WriteStats_();
            }
        }
        catch (...) {}
    }
}