LIBRARY()



SRCS(
    plot.cpp
    apply.cpp
    error_functions.cpp
    features_layout.cpp
    fold.cpp
    full_features.cpp
    greedy_tensor_search.cpp
    index_calcer.cpp
    index_hash_calcer.cpp
    learn_context.cpp
    online_ctr.cpp
    online_predictor.cpp
    ctr_helper.cpp
    score_calcer.cpp
    split.cpp
    target_classifier.cpp
    train.cpp
    train_one_iter_cross_entropy.cpp
    train_one_iter_custom.cpp
    train_one_iter_log_lin_quantile.cpp
    train_one_iter_logloss.cpp
    train_one_iter_map.cpp
    train_one_iter_multi_class.cpp
    train_one_iter_multi_class_one_vs_all.cpp
    train_one_iter_pair_logit.cpp
    train_one_iter_poisson.cpp
    train_one_iter_quantile.cpp
    train_one_iter_query_rmse.cpp
    train_one_iter_rmse.cpp
    train_one_iter_user_per_object.cpp
    train_one_iter_user_querywise.cpp
    tree_print.cpp
    helpers.cpp
    cv_data_partition.cpp
    calc_score_cache.cpp
)

PEERDIR(
    catboost/libs/data
    catboost/libs/helpers
    catboost/libs/logging
    catboost/libs/loggers
    catboost/libs/metrics
    catboost/libs/model
    catboost/libs/overfitting_detector
    library/binsaver
    library/containers/2d_array
    library/containers/dense_hash
    library/digest/md5
    library/dot_product
    library/fast_exp
    library/fast_log
    library/grid_creator
    library/json
    library/object_factory
    library/threading/local_executor
)

END()
