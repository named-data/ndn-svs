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

/**
 * A simple interface for a validator for data and interests
 */
class BaseValidator
{
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

/**
 * A simple interface for a signer for data and interests
 */
class BaseSigner
{
public:
  virtual void
  sign(Interest& interest) const {}

  virtual void
  sign(Data& data) const {}

  virtual ~BaseSigner() = default;

  security::SigningInfo signingInfo;
};

/**
 * A signer using an ndn-cxx keychain instance
 */
class KeyChainSigner : public BaseSigner {
public:
  KeyChainSigner(KeyChain& keyChain) : m_keyChain(keyChain) {}

  void
  sign(Interest& interest) const;

  void
  sign(Data& data) const;

  virtual ~KeyChainSigner() = default;
private:
  KeyChain& m_keyChain;
};

/**
 * Global security options for SVS instance
 */
class SecurityOptions
{
public:
  SecurityOptions(KeyChain& keyChain);

private:
  KeyChain& m_keyChain;

public:
  /** Signing options for sync interests */
  std::shared_ptr<BaseSigner> interestSigner;
  /** Signing options for data packets */
  std::shared_ptr<BaseSigner> dataSigner;
  /** Signing options for publication (encapsulated) packets */
  std::shared_ptr<BaseSigner> pubSigner;

  /** Validator to validate data and interests (unless using HMAC) */
  std::shared_ptr<BaseValidator> validator = 0;
  /** Validator to validate encapsulated data */
  std::shared_ptr<BaseValidator> encapsulatedDataValidator = 0;

  /** Number of retries on validation fail */
  int nRetriesOnValidationFail = 0;
  /** Interval before validation fail retry */
  int millisBeforeRetryOnValidationFail = 300;

  static KeyChain DEFAULT_KEYCHAIN;
  static const SecurityOptions DEFAULT;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_SIGNING_OPTIONS_HPP