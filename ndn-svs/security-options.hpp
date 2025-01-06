/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2025 University of California, Los Angeles
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

#ifndef NDN_SVS_SECURITY_OPTIONS_HPP
#define NDN_SVS_SECURITY_OPTIONS_HPP

#include "common.hpp"

namespace ndn::svs {

/**
 * A simple interface for a validator for data and interests
 */
class BaseValidator : noncopyable
{
public:
  virtual ~BaseValidator() = default;

  /**
   * @brief Asynchronously validate @p data
   *
   * @note @p successCb and @p failureCb must not be nullptr
   */
  virtual void validate(const Data& data,
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
  virtual void validate(const Interest& interest,
                        const ndn::security::InterestValidationSuccessCallback& successCb,
                        const ndn::security::InterestValidationFailureCallback& failureCb)
  {
    successCb(interest);
  }
};

/**
 * A simple interface for a signer for data and interests
 */
class BaseSigner : noncopyable
{
public:
  virtual ~BaseSigner();

  virtual void sign(Interest& interest) const {}

  virtual void sign(Data& data) const {}

public:
  security::SigningInfo signingInfo;
};

/**
 * A signer using an ndn-cxx keychain instance
 */
class KeyChainSigner : public BaseSigner
{
public:
  explicit KeyChainSigner(KeyChain& keyChain)
    : m_keyChain(keyChain)
  {
  }

  void sign(Interest& interest) const override;

  void sign(Data& data) const override;

private:
  KeyChain& m_keyChain;
};

/**
 * Global security options for SVS instance
 */
class SecurityOptions
{
public:
  explicit SecurityOptions(KeyChain& keyChain);

public:
  /** Signing options for sync interests */
  std::shared_ptr<BaseSigner> interestSigner;
  /** Signing options for data packets */
  std::shared_ptr<BaseSigner> dataSigner;
  /** Signing options for publication (encapsulated) packets */
  std::shared_ptr<BaseSigner> pubSigner;

  /** Validator to validate data and interests (unless using HMAC) */
  std::shared_ptr<BaseValidator> validator;
  /** Validator to validate encapsulated data */
  std::shared_ptr<BaseValidator> encapsulatedDataValidator;

  /** Number of retries on validation fail */
  int nRetriesOnValidationFail = 0;
  /** Interval before validation fail retry */
  int millisBeforeRetryOnValidationFail = 300;

  static inline KeyChain DEFAULT_KEYCHAIN;
  static const SecurityOptions DEFAULT;
};

} // namespace ndn::svs

#endif // NDN_SVS_SECURITY_OPTIONS_HPP
