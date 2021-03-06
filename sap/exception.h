/** \file exception.h
 *  \brief Definition of the SaP system_error exception class.
 */

#ifndef SAP_EXCEPTION_H
#define SAP_EXCEPTION_H

#include <stdexcept>
#include <string>

namespace sap {

class system_error : public std::runtime_error
{
public:
	enum Reason
	{
		Zero_pivoting        = -1,
		Negative_DB_weight = -2,
		Illegal_update       = -3,
		Illegal_solve        = -4,
		Matrix_singular      = -5
	};

	system_error(Reason             reason,
	             const std::string& what_arg)
	: std::runtime_error(what_arg),
	  m_reason(reason)
	{}

	system_error(Reason      reason,
	             const char* what_arg)
	: std::runtime_error(what_arg),
	  m_reason(reason)
	{}
	
	virtual ~system_error() throw() {}

	Reason  reason() const {return m_reason;}

private:
	Reason        m_reason;
};

} // namespace sap


#endif
