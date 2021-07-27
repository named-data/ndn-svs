/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2021 University of California, Los Angeles
 *
 * This file is part of ndn-svs, synchronization library for distributed realtime
 * applications for NDN.
 *
 * ndn-svs library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, in version 2.1 of the License.
 *
 * ndn-svs library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
 */

#ifndef NDN_SVS_VALIDATOR_HPP
#define NDN_SVS_VALIDATOR_HPP

#include <ndn-cxx/security/validator.hpp>

namespace ndn {
namespace svs {

class BaseValidator {
public:
    /**
     * @brief Asynchronously validate @p data
     *
     * @note @p successCb and @p failureCb must not be nullptr
     */
    virtual void
    validate(const Data& data,
             const ndn::security::DataValidationSuccessCallback& successCb,
             const ndn::security::DataValidationFailureCallback& failureCb)
    {
        successCb(data);
    }

    /**
     * @brief Asynchronously validate @p interest
     *
     * @note @p successCb and @p failureCb must not be nullptr
     */
    virtual void
    validate(const Interest& interest,
             const ndn::security::InterestValidationSuccessCallback& successCb,
             const ndn::security::InterestValidationFailureCallback& failureCb)
    {
        successCb(interest);
    }

    virtual ~BaseValidator() = default;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_VALIDATOR_HPP
