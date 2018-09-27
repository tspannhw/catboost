#include "util.h"

#include <catboost/libs/data_new/objects.h>

#include <catboost/libs/data_new/cat_feature_perfect_hash_helper.h>

#include <catboost/libs/helpers/compression.h>
#include <catboost/libs/helpers/math_utils.h>
#include <catboost/libs/helpers/vector_helpers.h>

#include <util/generic/maybe.h>
#include <util/random/easy.h>

#include <random>

#include <library/unittest/registar.h>

#include <util/stream/output.h>


using namespace NCB;


template <class TTObjectsDataProvider>
static TTObjectsDataProvider GetMaybeSubsetDataProvider(
    TTObjectsDataProvider&& objectsDataProvider,
    TMaybe<TArraySubsetIndexing<ui32>> subsetForGetSubset,
    NPar::TLocalExecutor* localExecutor
) {
    if (subsetForGetSubset.Defined()) {
        TObjectsGroupingSubset objectsGroupingSubset = GetSubset(
            objectsDataProvider.GetObjectsGrouping(),
            TArraySubsetIndexing<ui32>(*subsetForGetSubset)
        );

        auto subsetDataProvider = objectsDataProvider.GetSubset(objectsGroupingSubset, localExecutor);
        objectsDataProvider = std::move(
            dynamic_cast<TTObjectsDataProvider&>(*subsetDataProvider.Get())
        );
    }
    return std::move(objectsDataProvider);
}


template <class T, class TArrayLike>
void Compare(const TConstArrayRef<T>& lhs, const TArraySubset<TArrayLike, ui32>& rhs) {
    UNIT_ASSERT_VALUES_EQUAL(lhs.size(), rhs.Size());

    rhs.ForEach([&](ui32 idx, T element) {
        UNIT_ASSERT_VALUES_EQUAL(element, lhs[idx]);
    });
}

template <class T>
bool Equal(TMaybeData<TConstArrayRef<T>> lhs, TMaybeData<TVector<T>> rhs) {
    if (!lhs) {
        return !rhs;
    }
    if (!rhs) {
        return false;
    }
    return Equal(*lhs, *rhs);
}


Y_UNIT_TEST_SUITE(TRawObjectsData) {
    Y_UNIT_TEST(CommonObjectsData) {
        TVector<TObjectsGrouping> srcObjectsGroupings;
        TVector<TCommonObjectsData> srcDatas;

        {
            srcObjectsGroupings.push_back(TObjectsGrouping(ui32(4)));

            TCommonObjectsData commonData;
            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TFullSubset<ui32>(4)
            );

            srcDatas.push_back(commonData);
        }

        {
            srcObjectsGroupings.push_back(
                TObjectsGrouping(TVector<TGroupBounds>{{0, 2}, {2, 3}, {3, 4}, {4, 6}})
            );

            TCommonObjectsData commonData;

            TVector<TIndexRange<ui32>> indexRanges{{7, 10}, {2, 3}, {4, 6}};
            TSavedIndexRanges<ui32> savedIndexRanges(std::move(indexRanges));

            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TRangesSubset<ui32>(savedIndexRanges)
            );
            commonData.GroupIds = {0, 0, 2, 3, 4, 4};

            srcDatas.push_back(commonData);
        }

        {
            srcObjectsGroupings.push_back(
                TObjectsGrouping(TVector<TGroupBounds>{{0, 2}, {2, 3}, {3, 4}})
            );

            TCommonObjectsData commonData;
            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TIndexedSubset<ui32>{0, 4, 3, 1}
            );
            commonData.GroupIds = {0, 0, 3, 1};
            commonData.SubgroupIds = {0, 2, 3, 1};

            srcDatas.push_back(commonData);
        }

        {
            srcObjectsGroupings.push_back(TObjectsGrouping(ui32(4)));

            TCommonObjectsData commonData;
            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TFullSubset<ui32>(4)
            );
            commonData.Timestamp = {100, 0, 20, 10};

            srcDatas.push_back(commonData);
        }

        for (auto i : xrange(srcDatas.size())) {
            const auto& srcData = srcDatas[i];

            TFeaturesLayout featuresLayout(0, {}, {});

            NPar::TLocalExecutor localExecutor;
            localExecutor.RunAdditionalThreads(2);

            TRawObjectsDataProvider rawObjectsDataProvider(
                Nothing(),
                TCommonObjectsData{srcData},
                TRawObjectsData(),
                false,
                &featuresLayout,
                &localExecutor
            );

            UNIT_ASSERT_VALUES_EQUAL(
                rawObjectsDataProvider.GetObjectCount(),
                srcData.SubsetIndexing->Size()
            );
            UNIT_ASSERT_EQUAL(
                *rawObjectsDataProvider.GetObjectsGrouping(),
                srcObjectsGroupings[i]
            );

#define COMPARE_DATA_PROVIDER_FIELD(FIELD) \
            UNIT_ASSERT(Equal(rawObjectsDataProvider.Get##FIELD(), srcData.FIELD));

            COMPARE_DATA_PROVIDER_FIELD(GroupIds)
            COMPARE_DATA_PROVIDER_FIELD(SubgroupIds)
            COMPARE_DATA_PROVIDER_FIELD(Timestamp)

#undef COMPARE_DATA_PROVIDER_FIELD
        }
    }

    Y_UNIT_TEST(BadCommonObjectsData) {
        TVector<TCommonObjectsData> srcs;

        {
            TCommonObjectsData commonData;
            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TFullSubset<ui32>(4)
            );
            commonData.GroupIds = TVector<TGroupId>{0};

            srcs.push_back(commonData);
        }

        {
            TCommonObjectsData commonData;

            TVector<TIndexRange<ui32>> indexRanges{{7, 10}, {2, 3}, {4, 6}};
            TSavedIndexRanges<ui32> savedIndexRanges(std::move(indexRanges));

            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TRangesSubset<ui32>(savedIndexRanges)
            );
            commonData.GroupIds = {0, 0, 2, 3, 4, 0};

            srcs.push_back(commonData);
        }

        {
            TCommonObjectsData commonData;
            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TIndexedSubset<ui32>{0, 4, 3, 1}
            );
            commonData.SubgroupIds = {0, 2, 3, 1};

            srcs.push_back(commonData);
        }

        {
            TCommonObjectsData commonData;
            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TIndexedSubset<ui32>{0, 4, 3, 1}
            );
            commonData.GroupIds = {0, 0, 1, 1};
            commonData.SubgroupIds = {0, 2, 3};

            srcs.push_back(commonData);
        }

        {
            TCommonObjectsData commonData;
            commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
                TFullSubset<ui32>(4)
            );
            commonData.Timestamp = {0, 10};

            srcs.push_back(commonData);
        }

        for (auto& src : srcs) {
            TFeaturesLayout featuresLayout(0, {}, {});

            NPar::TLocalExecutor localExecutor;
            localExecutor.RunAdditionalThreads(2);

            UNIT_ASSERT_EXCEPTION(
                TRawObjectsDataProvider(
                    Nothing(),
                    std::move(src),
                    TRawObjectsData(),
                    false,
                    &featuresLayout,
                    &localExecutor
                ),
                TCatboostException
            );
        }
    }

    template <class T, EFeatureValuesType TType>
    void InitFeatures(
        const TVector<TVector<T>>& src,
        const TArraySubsetIndexing<ui32>& indexing,
        ui32* featureId,
        TVector<THolder<TArrayValuesHolder<T, TType>>>* dst
    ) {
        for (const auto& feature : src) {
            dst->emplace_back(
                MakeHolder<TArrayValuesHolder<T, TType>>(
                    (*featureId)++,
                    TMaybeOwningArrayHolder<T>::CreateOwning( TVector<T>(feature) ),
                    &indexing
                )
            );
        }
    }

    using TCatHashToString = std::pair<ui32, TString>;

    void TestFeatures(
        TMaybe<TArraySubsetIndexing<ui32>> subsetForGetSubset,
        const TObjectsGrouping& expectedObjectsGrouping,
        const TCommonObjectsData& commonData,

        // SubsetIndexing is not checked, only TRawObjectsDataProvider's fields
        const TCommonObjectsData& expectedCommonData,
        const TVector<TVector<float>>& srcFloatFeatures,
        const TVector<TVector<float>>& subsetFloatFeatures,
        const TVector<THashMap<ui32, TString>>& catFeaturesHashToString,
        const TVector<TVector<ui32>>& srcCatFeatures,
        const TVector<TVector<ui32>>& subsetCatFeatures
    ) {
        // (use float, use cat) pairs
        for (auto useFeatureTypes : {
            std::make_pair(true, false),
            std::make_pair(false, true),
            std::make_pair(true, true)
        }) {
            TRawObjectsData data;
            ui32 featureId = 0;

            TVector<ui32> catFeatureIndices;

            if (useFeatureTypes.first) {
                InitFeatures(srcFloatFeatures, *commonData.SubsetIndexing, &featureId, &data.FloatFeatures);
            }
            if (useFeatureTypes.second) {
                ui32 catFeaturesIndicesStart = featureId;

                data.CatFeaturesHashToString = MakeAtomicShared<TVector<THashMap<ui32, TString>>>(
                    catFeaturesHashToString
                );
                InitFeatures(srcCatFeatures, *commonData.SubsetIndexing, &featureId, &data.CatFeatures);

                for (ui32 idx : xrange(catFeaturesIndicesStart, featureId)) {
                    catFeatureIndices.push_back(idx);
                }
            }

            TFeaturesLayout featuresLayout(featureId, catFeatureIndices, {});

            NPar::TLocalExecutor localExecutor;
            localExecutor.RunAdditionalThreads(2);

            auto objectsDataProvider = GetMaybeSubsetDataProvider(
                TRawObjectsDataProvider(
                    Nothing(),
                    TCommonObjectsData{commonData},
                    std::move(data),
                    false,
                    &featuresLayout,
                    &localExecutor
                ),
                subsetForGetSubset,
                &localExecutor
            );

            UNIT_ASSERT_EQUAL(expectedObjectsGrouping, *objectsDataProvider.GetObjectsGrouping());

            if (useFeatureTypes.first) {
                for (auto i : xrange(subsetFloatFeatures.size())) {
                    Compare<float>(
                        subsetFloatFeatures[i],
                        (*objectsDataProvider.GetFloatFeature(i))->GetArrayData()
                    );
                }
            }
            if (useFeatureTypes.second) {
                for (auto i : xrange(subsetCatFeatures.size())) {
                    Compare<ui32>(
                        subsetCatFeatures[i],
                        (*objectsDataProvider.GetCatFeature(i))->GetArrayData()
                    );
                }
            }

#define COMPARE_DATA_PROVIDER_FIELD(FIELD) \
            UNIT_ASSERT(Equal(objectsDataProvider.Get##FIELD(), expectedCommonData.FIELD));

            COMPARE_DATA_PROVIDER_FIELD(GroupIds)
            COMPARE_DATA_PROVIDER_FIELD(SubgroupIds)
            COMPARE_DATA_PROVIDER_FIELD(Timestamp)

#undef COMPARE_DATA_PROVIDER_FIELD
        }
    }

    Y_UNIT_TEST(FullSubset) {
        TObjectsGrouping expectedObjectsGrouping(TVector<TGroupBounds>{{0, 2}, {2, 4}});

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TFullSubset<ui32>{4}
        );
        commonData.GroupIds = {0, 0, 1, 1};
        commonData.SubgroupIds = {0, 2, 3, 1};
        commonData.Timestamp = {10, 0, 100, 20};

        TVector<TVector<float>> floatFeatures = {
            {0.f, 1.f, 2.f, 2.3f},
            {0.22f, 0.3f, 0.16f, 0.f}
        };

        TVector<THashMap<ui32, TString>> catFeaturesHashToString;
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0x0, "0x0"),
                TCatHashToString(0x12, "0x12"),
                TCatHashToString(0x0F, "0x0F"),
                TCatHashToString(0x23, "0x23")
            };
            catFeaturesHashToString.push_back(hashToString);
        }
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0xAB, "0xAB"),
                TCatHashToString(0xBF, "0xBF"),
                TCatHashToString(0x04, "0x04"),
                TCatHashToString(0x20, "0x20")
            };
            catFeaturesHashToString.push_back(hashToString);
        }

        TVector<TVector<ui32>> catFeatures = {
            {0x0, 0x12, 0x0F, 0x23},
            {0xAB, 0xBF, 0x04, 0x20}
        };

        TestFeatures(
            Nothing(),
            expectedObjectsGrouping,
            commonData,
            commonData,
            floatFeatures,
            floatFeatures,
            catFeaturesHashToString,
            catFeatures,
            catFeatures
        );
    }

    Y_UNIT_TEST(Subset) {
        TObjectsGrouping expectedObjectsGrouping(TVector<TGroupBounds>{{0, 2}, {2, 4}});

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TIndexedSubset<ui32>{0, 4, 3, 1}
        );
        commonData.GroupIds = {0, 0, 1, 1};
        commonData.SubgroupIds = {0, 2, 3, 1};

        TVector<TVector<float>> srcFloatFeatures = {
            {0.f, 1.f, 2.f, 2.3f, 0.82f, 0.67f},
            {0.22f, 0.3f, 0.16f, 0.f, 0.2f, 0.11f},
            {0.31f, 1.0f, 0.23f, 0.89f, 0.0f, 0.9f}
        };

        TVector<TVector<float>> subsetFloatFeatures = {
            {0.f, 0.82f, 2.3f, 1.f},
            {0.22f, 0.2f, 0.f, 0.3f},
            {0.31f, 0.0f, 0.89f, 1.0f}
        };

        TVector<THashMap<ui32, TString>> catFeaturesHashToString;
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0x0, "0x0"),
                TCatHashToString(0x12, "0x12"),
                TCatHashToString(0x0F, "0x0F"),
                TCatHashToString(0x23, "0x23"),
                TCatHashToString(0x11, "0x11"),
                TCatHashToString(0x03, "0x03")
            };
            catFeaturesHashToString.push_back(hashToString);
        }
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0xAB, "0xAB"),
                TCatHashToString(0xBF, "0xBF"),
                TCatHashToString(0x04, "0x04"),
                TCatHashToString(0x20, "0x20"),
                TCatHashToString(0x78, "0x78"),
                TCatHashToString(0xFA, "0xFA")
            };
            catFeaturesHashToString.push_back(hashToString);
        }

        TVector<TVector<ui32>> srcCatFeatures = {
            {0x0, 0x12, 0x0F, 0x23, 0x11, 0x03},
            {0xAB, 0xBF, 0x04, 0x20, 0x78, 0xFA}
        };

        TVector<TVector<ui32>> subsetCatFeatures = {
            {0x0, 0x11, 0x23, 0x12},
            {0xAB, 0x78, 0x20, 0xBF}
        };

        TestFeatures(
            Nothing(),
            expectedObjectsGrouping,
            commonData,
            commonData,
            srcFloatFeatures,
            subsetFloatFeatures,
            catFeaturesHashToString,
            srcCatFeatures,
            subsetCatFeatures
        );
    }

    Y_UNIT_TEST(SubsetCompositionTrivialGrouping) {
        TArraySubsetIndexing<ui32> subsetForGetSubset(TIndexedSubset<ui32>{3,1});

        TObjectsGrouping expectedSubsetObjectsGrouping(ui32(2));

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TIndexedSubset<ui32>{0, 4, 3, 1}
        );

        TVector<TVector<float>> srcFloatFeatures = {
            {0.f, 1.f, 2.f, 2.3f, 0.82f, 0.67f},
            {0.22f, 0.3f, 0.16f, 0.f, 0.2f, 0.11f},
            {0.31f, 1.0f, 0.23f, 0.89f, 0.0f, 0.9f}
        };

        TVector<TVector<float>> subsetFloatFeatures = { {1.f, 0.82f}, {0.3f, 0.2f}, {1.0f, 0.0f} };

        TVector<THashMap<ui32, TString>> catFeaturesHashToString;
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0x0, "0x0"),
                TCatHashToString(0x12, "0x12"),
                TCatHashToString(0x0F, "0x0F"),
                TCatHashToString(0x23, "0x23"),
                TCatHashToString(0x11, "0x11"),
                TCatHashToString(0x03, "0x03")
            };
            catFeaturesHashToString.push_back(hashToString);
        }
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0xAB, "0xAB"),
                TCatHashToString(0xBF, "0xBF"),
                TCatHashToString(0x04, "0x04"),
                TCatHashToString(0x20, "0x20"),
                TCatHashToString(0x78, "0x78"),
                TCatHashToString(0xFA, "0xFA")
            };
            catFeaturesHashToString.push_back(hashToString);
        }

        TVector<TVector<ui32>> srcCatFeatures = {
            {0x0, 0x12, 0x0F, 0x23, 0x11, 0x03},
            {0xAB, 0xBF, 0x04, 0x20, 0x78, 0xFA}
        };

        TVector<TVector<ui32>> subsetCatFeatures = {{0x12, 0x11}, {0xBF, 0x78}};

        TestFeatures(
            subsetForGetSubset,
            expectedSubsetObjectsGrouping,
            commonData,
            TCommonObjectsData(),
            srcFloatFeatures,
            subsetFloatFeatures,
            catFeaturesHashToString,
            srcCatFeatures,
            subsetCatFeatures
        );
    }

    Y_UNIT_TEST(SubsetCompositionNonTrivialGrouping) {
        TArraySubsetIndexing<ui32> subsetForGetSubset(TIndexedSubset<ui32>{3,1});
        // expected indices of objects in src features arrays are: 6 8 9 4 3


        TObjectsGrouping expectedSubsetObjectsGrouping(TVector<TGroupBounds>{{0, 3}, {3, 5}});

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TIndexedSubset<ui32>{0, 4, 3, 1, 2, 6, 8, 9}
        );
        commonData.GroupIds = {0, 1, 1, 2, 2, 3, 3, 3};
        commonData.SubgroupIds = {0, 2, 3, 1, 7, 0, 2, 4};
        commonData.Timestamp = {10, 20, 15, 30, 25, 16, 22, 36};

        TCommonObjectsData expectedSubsetCommonData;
        expectedSubsetCommonData.GroupIds = {3, 3, 3, 1, 1};
        expectedSubsetCommonData.SubgroupIds = {0, 2, 4, 2, 3};
        expectedSubsetCommonData.Timestamp = {16, 22, 36, 20, 15};

        TVector<TVector<float>> srcFloatFeatures = {
            {0.f, 1.f, 2.f, 2.3f, 0.82f, 0.67f, 0.72f, 0.13f, 0.56f, 0.01f, 0.22f},
            {0.22f, 0.3f, 0.16f, 0.f, 0.2f, 0.11f, 0.98f, 0.22f, 0.73f, 0.01f, 0.64f},
            {0.31f, 1.0f, 0.23f, 0.89f, 0.0f, 0.9f, 0.45f, 0.72f, 0.88f, 0.11f, 0.03f},
        };


        TVector<TVector<float>> subsetFloatFeatures = {
            {0.72f, 0.56f, 0.01f, 0.82f, 2.3f},
            {0.98f, 0.73f, 0.01f, 0.2f, 0.f},
            {0.45f, 0.88f, 0.11f, 0.0f, 0.89f}
        };

        TVector<THashMap<ui32, TString>> catFeaturesHashToString;
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0x0, "0x0"),
                TCatHashToString(0x12, "0x12"),
                TCatHashToString(0x0F, "0x0F"),
                TCatHashToString(0x23, "0x23"),
                TCatHashToString(0x11, "0x11"),
                TCatHashToString(0x03, "0x03"),
                TCatHashToString(0x18, "0x18"),
                TCatHashToString(0xA3, "0xA3"),
                TCatHashToString(0x0B, "0x0B"),
                TCatHashToString(0x34, "0x34"),
                TCatHashToString(0x71, "0x71"),
            };
            catFeaturesHashToString.push_back(hashToString);
        }
        {
            THashMap<ui32, TString> hashToString = {
                TCatHashToString(0xAB, "0xAB"),
                TCatHashToString(0xBF, "0xBF"),
                TCatHashToString(0x04, "0x04"),
                TCatHashToString(0x20, "0x20"),
                TCatHashToString(0x78, "0x78"),
                TCatHashToString(0xFA, "0xFA"),
                TCatHashToString(0xAC, "0xAC"),
                TCatHashToString(0x91, "0x91"),
                TCatHashToString(0x02, "0x02"),
                TCatHashToString(0x99, "0x99"),
                TCatHashToString(0xAA, "0xAA")
            };
            catFeaturesHashToString.push_back(hashToString);
        }

        TVector<TVector<ui32>> srcCatFeatures = {
            {0x00, 0x12, 0x0F, 0x23, 0x11, 0x03, 0x18, 0xA3, 0x0B, 0x34, 0x71},
            {0xAB, 0xBF, 0x04, 0x20, 0x78, 0xFA, 0xAC, 0x91, 0x02, 0x99, 0xAA}
        };


        TVector<TVector<ui32>> subsetCatFeatures = {
            {0x18, 0x0B, 0x34, 0x11, 0x23},
            {0xAC, 0x02, 0x99, 0x78, 0x20}
        };


        TestFeatures(
            subsetForGetSubset,
            expectedSubsetObjectsGrouping,
            commonData,
            expectedSubsetCommonData,
            srcFloatFeatures,
            subsetFloatFeatures,
            catFeaturesHashToString,
            srcCatFeatures,
            subsetCatFeatures
        );
    }

}


Y_UNIT_TEST_SUITE(TQuantizedObjectsData) {

    TVector<ui32> GenerateSrcHashedCatData(ui32 uniqValues) {
        TVector<ui32> result;

        for (ui32 value : xrange(uniqValues)) {
            // repeat each value from 1 to 10 times, simple rand() is ok for this purpose
            ui32 repetitionCount = ui32(1 + (std::rand() % 10));
            for (auto i : xrange(repetitionCount)) {
                Y_UNUSED(i);
                result.push_back(value);
            }
        }

        std::random_device rd;
        std::mt19937 g(rd());

        std::shuffle(result.begin(), result.end(), g);

        return result;
    }


    void TestFeatures(
        TMaybe<TArraySubsetIndexing<ui32>> subsetForGetSubset,
        const TObjectsGrouping& expectedObjectsGrouping,
        const TCommonObjectsData& commonData,

        // SubsetIndexing is not checked, only TRawObjectsDataProvider's fields
        const TCommonObjectsData& expectedCommonData,

        // for getting bitsPerKey for GPU
        const TVector<ui32>& srcFloatFeatureBinCounts,
        const TVector<TVector<ui8>>& srcFloatFeatures,
        const TVector<TVector<ui8>>& subsetFloatFeatures,

        // for initialization of uniq values data in TQuantizedFeaturesManager
        const TVector<ui32>& srcUniqHashedCatValues,
        const TVector<TVector<ui32>>& srcCatFeatures,
        const TVector<TVector<ui32>>& subsetCatFeatures
    ) {
        for (auto taskType : NCB::NDataNewUT::GetTaskTypes()) {
            NCatboostOptions::TCatFeatureParams catFeatureParams(taskType);

            // (use float, use cat) pairs
            for (auto useFeatureTypes : {
                std::make_pair(true, false),
                std::make_pair(false, true),
                std::make_pair(true, true)
            }) {
                TQuantizedObjectsData data;
                data.Manager = MakeIntrusive<TQuantizedFeaturesManager>(
                    catFeatureParams,
                    NCatboostOptions::TBinarizationOptions()
                );

                ui32 featureId = 0;
                TVector<ui32> catFeatureIndices;

                if (useFeatureTypes.first) {
                    for (auto floatFeatureIdx : xrange(srcFloatFeatures.size())) {
                        const auto& floatFeature = srcFloatFeatures[floatFeatureIdx];
                        ui32 bitsPerKey =
                            (taskType == ETaskType::CPU) ?
                            8 :
                            IntLog2(srcFloatFeatureBinCounts[floatFeatureIdx]);

                        auto storage = TMaybeOwningArrayHolder<ui64>::CreateOwning(
                            CompressVector<ui64>(floatFeature.data(), floatFeature.size(), bitsPerKey)
                        );

                        data.Manager->RegisterDataProviderFloatFeature(featureId);

                        data.FloatFeatures.emplace_back(
                            MakeHolder<TQuantizedFloatValuesHolder>(
                                featureId,
                                TCompressedArray(floatFeature.size(), bitsPerKey, storage),
                                commonData.SubsetIndexing.Get()
                            )
                        );
                        ++featureId;
                    }
                }

                if (useFeatureTypes.second) {
                    TCatFeaturesPerfectHashHelper catFeaturesPerfectHashHelper(data.Manager);

                    for (auto catFeatureIdx : xrange(srcCatFeatures.size())) {
                        const auto& catFeature = srcCatFeatures[catFeatureIdx];
                        auto hashedCatValues = TMaybeOwningArrayHolder<ui32>::CreateOwning(
                            GenerateSrcHashedCatData(srcUniqHashedCatValues[catFeatureIdx])
                        );

                        TArraySubsetIndexing<ui32> fullSubsetForUpdatingPerfectHash(
                            TFullSubset<ui32>((*hashedCatValues).size())
                        );

                        data.Manager->RegisterDataProviderCatFeature(featureId);

                        catFeaturesPerfectHashHelper.UpdatePerfectHashAndMaybeQuantize(
                            featureId,
                            TMaybeOwningArraySubset<ui32, ui32>(
                                &hashedCatValues,
                                &fullSubsetForUpdatingPerfectHash
                            ),
                            Nothing()
                        );

                        ui32 bitsPerKey =
                            (taskType == ETaskType::CPU) ?
                            32 :
                            IntLog2(catFeaturesPerfectHashHelper.GetUniqueValues(featureId));

                        auto storage = TMaybeOwningArrayHolder<ui64>::CreateOwning(
                            CompressVector<ui64>(catFeature.data(), catFeature.size(), bitsPerKey)
                        );

                        data.CatFeatures.emplace_back(
                            MakeHolder<TQuantizedCatValuesHolder>(
                                featureId,
                                TCompressedArray(catFeature.size(), bitsPerKey, storage),
                                commonData.SubsetIndexing.Get()
                            )
                        );

                        catFeatureIndices.push_back(featureId);

                        ++featureId;
                    }
                }

                TFeaturesLayout featuresLayout(featureId, catFeatureIndices, {});

                NPar::TLocalExecutor localExecutor;
                localExecutor.RunAdditionalThreads(2);

                THolder<TQuantizedObjectsDataProvider> objectsDataProvider;

                if (taskType == ETaskType::CPU) {
                    objectsDataProvider = MakeHolder<TQuantizedForCPUObjectsDataProvider>(
                        GetMaybeSubsetDataProvider(
                            TQuantizedForCPUObjectsDataProvider(
                                Nothing(),
                                TCommonObjectsData(commonData),
                                std::move(data),
                                false,
                                &featuresLayout
                            ),
                            subsetForGetSubset,
                            &localExecutor
                        )
                    );
                } else {
                    objectsDataProvider = MakeHolder<TQuantizedObjectsDataProvider>(
                        GetMaybeSubsetDataProvider(
                            TQuantizedObjectsDataProvider(
                                Nothing(),
                                TCommonObjectsData(commonData),
                                std::move(data),
                                false,
                                &featuresLayout
                            ),
                            subsetForGetSubset,
                            &localExecutor
                        )
                    );
                }

                UNIT_ASSERT_EQUAL(
                    *objectsDataProvider->GetObjectsGrouping(),
                    expectedObjectsGrouping
                );

                if (useFeatureTypes.first) {
                    for (auto i : xrange(subsetFloatFeatures.size())) {
                        UNIT_ASSERT_EQUAL(
                            (TConstArrayRef<ui8>)subsetFloatFeatures[i],
                            *((*objectsDataProvider->GetFloatFeature(i))->ExtractValues(
                                &localExecutor
                            ))
                        );
                    }
                }
                if (useFeatureTypes.second) {
                    for (auto i : xrange(subsetCatFeatures.size())) {
                        UNIT_ASSERT_EQUAL(
                            (TConstArrayRef<ui32>)subsetCatFeatures[i],
                            *((*objectsDataProvider->GetCatFeature(i))->ExtractValues(&localExecutor))
                        );
                    }
                }
                if (taskType == ETaskType::CPU) {
                    auto& quantizedForCPUObjectsDataProvider =
                        dynamic_cast<TQuantizedForCPUObjectsDataProvider&>(*objectsDataProvider);

                    if (useFeatureTypes.first) {
                        for (auto i : xrange(subsetFloatFeatures.size())) {
                            Compare(
                                (TConstArrayRef<ui8>)subsetFloatFeatures[i],
                                (*quantizedForCPUObjectsDataProvider.GetFloatFeature(i))->GetArrayData()
                            );
                        }
                    }

                    if (useFeatureTypes.second) {
                        for (auto i : xrange(subsetCatFeatures.size())) {
                            Compare(
                                (TConstArrayRef<ui32>)subsetCatFeatures[i],
                                (*quantizedForCPUObjectsDataProvider.GetCatFeature(i))->GetArrayData()
                            );

                            UNIT_ASSERT_VALUES_EQUAL(
                                quantizedForCPUObjectsDataProvider.GetUseCatFeatureForOneHot(i),
                                (srcUniqHashedCatValues[i] > 0) &&
                                (srcUniqHashedCatValues[i] <= catFeatureParams.OneHotMaxSize)
                            );
                            UNIT_ASSERT_VALUES_EQUAL(
                                quantizedForCPUObjectsDataProvider.GetCatFeatureUniqueValuesCount(i),
                                srcUniqHashedCatValues[i]
                            );
                        }
                    }
                }

#define COMPARE_DATA_PROVIDER_FIELD(FIELD) \
            UNIT_ASSERT(Equal(objectsDataProvider->Get##FIELD(), expectedCommonData.FIELD));

            COMPARE_DATA_PROVIDER_FIELD(GroupIds)
            COMPARE_DATA_PROVIDER_FIELD(SubgroupIds)
            COMPARE_DATA_PROVIDER_FIELD(Timestamp)

#undef COMPARE_DATA_PROVIDER_FIELD
            }
        }
    }


    Y_UNIT_TEST(FullSubset) {
        TObjectsGrouping expectedObjectsGrouping(TVector<TGroupBounds>{{0, 2}, {2, 4}});

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TFullSubset<ui32>{4}
        );
        commonData.GroupIds = {0, 0, 1, 1};
        commonData.SubgroupIds = {0, 2, 3, 1};
        commonData.Timestamp = {10, 0, 100, 20};

        TVector<ui32> srcFloatFeatureBinCounts = {32, 256};

        TVector<TVector<ui8>> floatFeatures = {{0x01, 0x12, 0x11, 0x03}, {0x22, 0x10, 0x01, 0xAF}};

        TVector<ui32> srcUniqHashedCatValues = {128, 511};

        TVector<TVector<ui32>> catFeatures = {{0x0, 0x02, 0x0F, 0x03}, {0xAB, 0xBF, 0x04, 0x20}};

        TestFeatures(
            Nothing(),
            expectedObjectsGrouping,
            commonData,
            commonData,
            srcFloatFeatureBinCounts,
            floatFeatures,
            floatFeatures,
            srcUniqHashedCatValues,
            catFeatures,
            catFeatures
        );
    }

    Y_UNIT_TEST(Subset) {
        TObjectsGrouping expectedObjectsGrouping(TVector<TGroupBounds>{{0, 2}, {2, 4}});

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TIndexedSubset<ui32>{0, 4, 3, 1}
        );
        commonData.GroupIds = {0, 0, 1, 1};
        commonData.SubgroupIds = {0, 2, 3, 1};

        TVector<ui32> srcFloatFeatureBinCounts = {64, 256, 256};

        TVector<TVector<ui8>> srcFloatFeatures = {
            {0x0, 0x12, 0x0F, 0x23, 0x11, 0x01},
            {0xAB, 0xBF, 0x04, 0x20, 0xAA, 0x12},
            {0x10, 0x02, 0x01, 0xFA, 0xFF, 0x11}
        };

        TVector<TVector<ui8>> subsetFloatFeatures = {
            {0x0, 0x11, 0x23, 0x12},
            {0xAB, 0xAA, 0x20, 0xBF},
            {0x10, 0xFF, 0xFA, 0x02}
        };

        TVector<ui32> srcUniqHashedCatValues = {128, 511};

        TVector<TVector<ui32>> srcCatFeatures = {
            {0x0, 0x02, 0x0F, 0x03, 0x01, 0x03},
            {0xAB, 0xBF, 0x04, 0x20, 0x78, 0xFA}
        };

        TVector<TVector<ui32>> subsetCatFeatures = {{0x0, 0x01, 0x03, 0x02}, {0xAB, 0x78, 0x20, 0xBF}};


        TestFeatures(
            Nothing(),
            expectedObjectsGrouping,
            commonData,
            commonData,
            srcFloatFeatureBinCounts,
            srcFloatFeatures,
            subsetFloatFeatures,
            srcUniqHashedCatValues,
            srcCatFeatures,
            subsetCatFeatures
        );
    }

    Y_UNIT_TEST(SubsetCompositionTrivialGrouping) {
        TArraySubsetIndexing<ui32> subsetForGetSubset(TIndexedSubset<ui32>{3,1});

        TObjectsGrouping expectedSubsetObjectsGrouping(ui32(2));

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TIndexedSubset<ui32>{0, 4, 3, 1}
        );

        TVector<ui32> srcFloatFeatureBinCounts = {64, 256, 256};

        TVector<TVector<ui8>> srcFloatFeatures = {
            {0x0, 0x12, 0x0F, 0x23, 0x11, 0x01},
            {0xAB, 0xBF, 0x04, 0x20, 0xAA, 0x12},
            {0x10, 0x02, 0x01, 0xFA, 0xFF, 0x11}
        };

        TVector<TVector<ui8>> subsetFloatFeatures = {{0x12, 0x11}, {0xBF, 0xAA}, {0x02, 0xFF}};

        TVector<ui32> srcUniqHashedCatValues = {128, 511};

        TVector<TVector<ui32>> srcCatFeatures = {
            {0x00, 0x02, 0x0F, 0x03, 0x01, 0x03},
            {0xAB, 0xBF, 0x04, 0x20, 0x78, 0xFA}
        };

        TVector<TVector<ui32>> subsetCatFeatures = {{0x02, 0x01}, {0xBF, 0x78}};

        TestFeatures(
            subsetForGetSubset,
            expectedSubsetObjectsGrouping,
            commonData,
            TCommonObjectsData(),
            srcFloatFeatureBinCounts,
            srcFloatFeatures,
            subsetFloatFeatures,
            srcUniqHashedCatValues,
            srcCatFeatures,
            subsetCatFeatures
        );
    }

    Y_UNIT_TEST(SubsetCompositionNonTrivialGrouping) {
        TArraySubsetIndexing<ui32> subsetForGetSubset(TIndexedSubset<ui32>{3,1});
        // expected indices of objects in src features arrays are: 6 8 9 4 3

        TObjectsGrouping expectedSubsetObjectsGrouping(TVector<TGroupBounds>{{0, 3}, {3, 5}});

        TCommonObjectsData commonData;

        commonData.SubsetIndexing = MakeAtomicShared<TArraySubsetIndexing<ui32>>(
            TIndexedSubset<ui32>{0, 4, 3, 1, 2, 6, 8, 9}
        );
        commonData.GroupIds = {0, 1, 1, 2, 2, 3, 3, 3};
        commonData.SubgroupIds = {0, 2, 3, 1, 7, 0, 2, 4};
        commonData.Timestamp = {10, 20, 15, 30, 25, 16, 22, 36};

        TCommonObjectsData expectedSubsetCommonData;
        expectedSubsetCommonData.GroupIds = {3, 3, 3, 1, 1};
        expectedSubsetCommonData.SubgroupIds = {0, 2, 4, 2, 3};
        expectedSubsetCommonData.Timestamp = {16, 22, 36, 20, 15};


        TVector<ui32> srcFloatFeatureBinCounts = {64, 256, 256};

        TVector<TVector<ui8>> srcFloatFeatures = {
            {0x00, 0x12, 0x0F, 0x23, 0x11, 0x01, 0x32, 0x18, 0x22, 0x05, 0x19},
            {0xAB, 0xBF, 0x04, 0x20, 0xAA, 0x12, 0xF2, 0xEE, 0x18, 0x00, 0x90},
            {0x10, 0x02, 0x01, 0xFA, 0xFF, 0x11, 0xFA, 0xFB, 0xAA, 0xAB, 0x00}
        };

        TVector<TVector<ui8>> subsetFloatFeatures = {
            {0x32, 0x22, 0x05, 0x11, 0x23},
            {0xF2, 0x18, 0x00, 0xAA, 0x20},
            {0xFA, 0xAA, 0xAB, 0xFF, 0xFA}
        };

        TVector<ui32> srcUniqHashedCatValues = {128, 511};

        TVector<TVector<ui32>> srcCatFeatures = {
            {0x00, 0x02, 0x0F, 0x03, 0x01, 0x03, 0x72, 0x6B, 0x5A, 0x11, 0x04},
            {0xAB, 0xBF, 0x04, 0x20, 0x78, 0xFA, 0xFF, 0x78, 0x89, 0xFA, 0x3B}
        };

        TVector<TVector<ui32>> subsetCatFeatures = {
            {0x72, 0x5A, 0x11, 0x01, 0x03},
            {0xFF, 0x89, 0xFA, 0x78, 0x20}
        };

        TestFeatures(
            subsetForGetSubset,
            expectedSubsetObjectsGrouping,
            commonData,
            expectedSubsetCommonData,
            srcFloatFeatureBinCounts,
            srcFloatFeatures,
            subsetFloatFeatures,
            srcUniqHashedCatValues,
            srcCatFeatures,
            subsetCatFeatures
        );

    }
}