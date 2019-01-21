/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Generic.hh"
#include <sstream>

namespace avro {

using std::string;
using std::vector;
using std::ostringstream;

typedef vector<uint8_t> bytes;

void GenericContainer::assertType(const NodePtr& schema, Type type) {
    if (schema->type() != type) {
        throw Exception(boost::format("Schema type %1 expected %2") %
            toString(schema->type()) % toString(type));
    }
}

GenericReader::GenericReader(const ValidSchema& s, const DecoderPtr& decoder) :
    schema_(s), isResolving_(static_cast<ResolvingDecoder*>(&(*decoder)) != 0),
    decoder_(decoder)
{
}

GenericReader::GenericReader(const ValidSchema& writerSchema,
    const ValidSchema& readerSchema, const DecoderPtr& decoder) :
    schema_(readerSchema),
    isResolving_(true),
    decoder_(resolvingDecoder(writerSchema, readerSchema, decoder))
{
}

void GenericReader::read(GenericDatum& datum) const
{
    datum = GenericDatum(schema_.root());
    read(datum, *decoder_, isResolving_);
}

void GenericReader::read(GenericDatum& datum, Decoder& d, bool isResolving)
{
    if (datum.isUnion()) {
        datum.selectBranch(d.decodeUnionIndex());
    }
    switch (datum.type()) {
    case AVRO_NULL:
        d.decodeNull();
        break;
    case AVRO_BOOL:
        datum.value<bool>() = d.decodeBool();
        break;
    case AVRO_INT:
        datum.value<int32_t>() = d.decodeInt();
        break;
    case AVRO_LONG:
        datum.value<int64_t>() = d.decodeLong();
        break;
    case AVRO_FLOAT:
        datum.value<float>() = d.decodeFloat();
        break;
    case AVRO_DOUBLE:
        datum.value<double>() = d.decodeDouble();
        break;
    case AVRO_STRING:
        d.decodeString(datum.value<string>());
        break;
    case AVRO_BYTES:
        d.decodeBytes(datum.value<bytes>());
        break;
    case AVRO_FIXED:
        {
            GenericFixed& f = datum.value<GenericFixed>();
            d.decodeFixed(f.schema()->fixedSize(), f.value());
        }
        break;
    case AVRO_RECORD:
        {
            GenericRecord& r = datum.value<GenericRecord>();
            size_t c = r.schema()->leaves();
            if (isResolving) {
                std::vector<size_t> fo =
                    static_cast<ResolvingDecoder&>(d).fieldOrder();
                for (size_t i = 0; i < c; ++i) {
                    read(r.fieldAt(fo[i]), d, isResolving);
                }
            } else {
                for (size_t i = 0; i < c; ++i) {
                    read(r.fieldAt(i), d, isResolving);
                }
            }
        }
        break;
    case AVRO_ENUM:
        datum.value<GenericEnum>().set(d.decodeEnum());
        break;
    case AVRO_ARRAY:
        {
            GenericArray& v = datum.value<GenericArray>();
            vector<GenericDatum>& r = v.value();
            const NodePtr& nn = v.schema()->leafAt(0);
            r.resize(0);
            size_t start = 0;
            for (size_t m = d.arrayStart(); m != 0; m = d.arrayNext()) {
                r.resize(r.size() + m);
                for (; start < r.size(); ++start) {
                    r[start] = GenericDatum(nn);
                    read(r[start], d, isResolving);
                }
            }
        }
        break;
    case AVRO_MAP:
        {
            GenericMap& v = datum.value<GenericMap>();
            GenericMap::Value& r = v.value();
            const NodePtr& nn = v.schema()->leafAt(1);
            r.resize(0);
            size_t start = 0;
            for (size_t m = d.mapStart(); m != 0; m = d.mapNext()) {
                r.resize(r.size() + m);
                for (; start < r.size(); ++start) {
                    d.decodeString(r[start].first);
                    r[start].second = GenericDatum(nn);
                    read(r[start].second, d, isResolving);
                }
            }
        }
        break;
    default:
        throw Exception(boost::format("Unknown schema type %1%") %
            toString(datum.type()));
    }
}

void GenericReader::read(Decoder& d, GenericDatum& g, const ValidSchema& s)
{
    g = GenericDatum(s);
    read(d, g);
}

void GenericReader::read(Decoder& d, GenericDatum& g)
{
    read(g, d, static_cast<ResolvingDecoder*>(&d) != 0);
}

GenericWriter::GenericWriter(const ValidSchema& s, const EncoderPtr& encoder) :
    schema_(s), encoder_(encoder)
{
}

void GenericWriter::write(const GenericDatum& datum) const
{
    write(datum, *encoder_);
}

void GenericWriter::write(const GenericDatum& datum, Encoder& e)
{
    if (datum.isUnion()) {
        e.encodeUnionIndex(datum.unionBranch());
    }
    switch (datum.type()) {
    case AVRO_NULL:
        e.encodeNull();
        break;
    case AVRO_BOOL:
        e.encodeBool(datum.value<bool>());
        break;
    case AVRO_INT:
        e.encodeInt(datum.value<int32_t>());
        break;
    case AVRO_LONG:
        e.encodeLong(datum.value<int64_t>());
        break;
    case AVRO_FLOAT:
        e.encodeFloat(datum.value<float>());
        break;
    case AVRO_DOUBLE:
        e.encodeDouble(datum.value<double>());
        break;
    case AVRO_STRING:
        e.encodeString(datum.value<string>());
        break;
    case AVRO_BYTES:
        e.encodeBytes(datum.value<bytes>());
        break;
    case AVRO_FIXED:
        e.encodeFixed(datum.value<GenericFixed>().value());
        break;
    case AVRO_RECORD:
        {
            const GenericRecord& r = datum.value<GenericRecord>();
            size_t c = r.schema()->leaves();
            for (size_t i = 0; i < c; ++i) {
                write(r.fieldAt(i), e);
            }
        }
        break;
    case AVRO_ENUM:
        e.encodeEnum(datum.value<GenericEnum>().value());
        break;
    case AVRO_ARRAY:
        {
            const GenericArray::Value& r = datum.value<GenericArray>().value();
            e.arrayStart();
            if (! r.empty()) {
                e.setItemCount(r.size());
                for (GenericArray::Value::const_iterator it = r.begin();
                    it != r.end(); ++it) {
                    e.startItem();
                    write(*it, e);
                }
            }
            e.arrayEnd();
        }
        break;
    case AVRO_MAP:
        {
            const GenericMap::Value& r = datum.value<GenericMap>().value();
            e.mapStart();
            if (! r.empty()) {
                e.setItemCount(r.size());
                for (GenericMap::Value::const_iterator it = r.begin();
                    it != r.end(); ++it) {
                    e.startItem();
                    e.encodeString(it->first);
                    write(it->second, e);
                }
            }
            e.mapEnd();
        }
        break;
    default:
        throw Exception(boost::format("Unknown schema type %1%") %
            toString(datum.type()));
    }
}

void GenericWriter::write(Encoder& e, const GenericDatum& g)
{
    write(g, e);
}

}   // namespace avro
