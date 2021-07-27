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

#ifndef NDN_SVS_SIGNING_OPTIONS_HPP
#define NDN_SVS_SIGNING_OPTIONS_HPP

#include "common.hpp"

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

class BaseSigner {
public:
  BaseSigner() : m_keyChain(new KeyChain("pib-memory:", "tpm-memory:")) {}

  BaseSigner(KeyChain& keyChain) : m_keyChain(&keyChain) {}

  virtual void
  sign(Interest& interest) const
  {
    m_keyChain->sign(interest, signingInfo);
  }

  virtual void
  sign(Data& data) const
  {
    m_keyChain->sign(data, signingInfo);
  }

  virtual ~BaseSigner() = default;

  security::SigningInfo signingInfo;

private:
  std::shared_ptr<KeyChain> m_keyChain;
};

struct SecurityOptions
{
  SecurityOptions() : m_keyChain(new KeyChain()) {
    init();
  }

  SecurityOptions(KeyChain& keyChain) : m_keyChain(&keyChain) {
    init();
  }

  /** Signing options for sync interests */
  std::shared_ptr<BaseSigner> interestSigner;
  /** Signing options for data packets */
  std::shared_ptr<BaseSigner> dataSigner;
  /** Signing options for publication (encapsulated) packets */
  std::shared_ptr<BaseSigner> pubSigner;

  /** Validator to validate data and interests (unless using HMAC) */
  std::shared_ptr<BaseValidator> validator = DEFAULT_VALIDATOR;

  static const SecurityOptions DEFAULT;
  static const std::shared_ptr<BaseValidator> DEFAULT_VALIDATOR;

private:
  void
  init()
  {
    interestSigner = make_shared<BaseSigner>(*m_keyChain);
    dataSigner = make_shared<BaseSigner>(*m_keyChain);
    pubSigner = make_shared<BaseSigner>(*m_keyChain);

    // Interest signing format
    interestSigner->signingInfo.setSignedInterestFormat(security::SignedInterestFormat::V03);
  }

private:
  std::shared_ptr<KeyChain> m_keyChain;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_SIGNING_OPTIONS_HPP