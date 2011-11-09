require 'rexml/document'
include REXML
require 'rack/request'
require 'rack/response'
require 'pp'

module Rack
  class Conductor
    def call(env)
        #puts PP.pp(env)

        # The request object only auto parses form data into .params
        # but we'll use it as if provides some simple and handy
        # wrapping of various http headers.
        req = Request.new(env)

        if req.path !~ /conductor\/api\/hooks\/?(\d+)?$/
            return [404,{"Content-Type" => "text/html"},"not found"]
        end
        if req.post? && req.content_type != 'application/xml'
            return [415,{"Content-Type" => "text/html"}, "invalid Content-type"]
        end
        if req.post?
            return post(env)
        elsif req.get?
            return get(env)
        elsif req.delete?
            return delete(env)
        else
            return [501,{"Content-Type" => "text/html"},"method not supported"]
        end
    end

    # Return all hooks, or the specified hook.
    # The body will be the confirmation XML hook definition
    # generated and returned by the post method.
    def get(env)
        hook_id = env['REQUEST_PATH'].scan(/(\d+)/)[0].to_s
        if hook_id == ''
            hook_id = '1234' # just handle this one for now
            xml_response="<hooks>\n"
            if ::File.exists?("hook."+hook_id)
                xml_response+=::File.open("hook."+hook_id, 'r').read
            end
            xml_response+="</hooks>\n"
            return [200,{"Content-Type" => "application/xml"},xml_response]
        elsif hook_id == '1234'
            return [200,{"Content-Type" => "application/xml"},
                    ::File.open("hook."+hook_id, 'r').read]
        else
            return [404,{"Content-Type" => "text/html"},"not found"]
        end
    end

    # Remove the hook file if present,
    # or return 404 otherwise.
    def delete(env)
        hook_id = env['REQUEST_PATH'].scan(/(\d+)/)[0].to_s
        if !::File.exists?("hook."+hook_id)
            return [404,{"Content-Type" => "text/html"},"not found"]
        end
        ::File.delete("hook."+hook_id)
        return [204,{"Content-Type" => "text/html"},""]
    end

    # Parse the XML from the CPE agent which contains the hook URL.
    # Return this as confirmation in the XML body, and also set
    # the 'Location' header to which the CPE will communicate.
    def post(env)
        post_body = env['rack.input'].read

        begin
            doc = Document.new post_body
            cpe_uri = doc.elements["*/uri"]
            if cpe_uri.nil?
                raise 'missing uri element'
            else
                cpe_uri = cpe_uri.get_text.to_s
            end
            cpe_ver = doc.elements["*/version"]
            if cpe_ver.nil?
                raise 'missing version element'
            else
                cpe_ver = cpe_ver.get_text.to_s
            end
        rescue Exception => error
            return [400,{"Content-Type" => "text/plain"}, error.to_s]
        end

        if Integer(cpe_ver) != 1
            return [501,{"Content-Type" => "text/html"},"version not supported"]
        end

        hook_id = 1234.to_s # We currently only support a single cpe agent

        xml_response  = ""
        xml_response += "<hook id=\"" + hook_id + "\" href=\"/api/hooks/" + hook_id + "\">\n"
        xml_response += "  <uri>" + cpe_uri + "</uri>\n"
        xml_response += "  <version>" + cpe_ver + "</version>\n"
        xml_response += "</hook>\n"

        # Record the hook for lookup later
        ::File.open("hook."+hook_id, 'w') {|f| f.write(xml_response) }

        location = env['REQUEST_URI'] + '/' + hook_id;

        return [201,{"Content-Type" => "application/xml",
                     "Location" => location},
                xml_response]

        # TODO: Populate the cped with a test deployment using:
        # Call ./cpe-rest test_started 1234
    end
  end
end
