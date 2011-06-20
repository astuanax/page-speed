/**
 * Copyright 2009 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Testing

#ifndef PAGESPEED_CSSMIN_CSSMIN_H_
#define PAGESPEED_CSSMIN_CSSMIN_H_

#include <string>

namespace pagespeed {

namespace cssmin {

// Minifies CSS by removing comments and whitespaces.
bool MinifyCss(const std::string& input, std::string* out);

// Calculate the minified size without actually constructing the minified
// output.
bool GetMinifiedCssSize(const std::string& input, int* minified_size);

}  // namespace cssmin

}  // namespace pagespeed

#endif  // PAGESPEED_CSSMIN_CSSMIN_H_
