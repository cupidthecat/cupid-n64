function(cupid_renderer_replace path before after)
  file(READ "${path}" content)
  string(FIND "${content}" "${before}" position)
  if(position LESS 0)
    message(FATAL_ERROR "Renderer correction no longer matches the pinned ${path}")
  endif()
  string(REPLACE "${before}" "${after}" content "${content}")
  file(WRITE "${path}" "${content}")
endfunction()

set(renderer_fixed_backend "${CMAKE_CURRENT_BINARY_DIR}/renderer-fixes/vulkan")
file(MAKE_DIRECTORY "${renderer_fixed_backend}")
configure_file("${renderer_backend_SOURCE_DIR}/vulkan/render_pass.cpp"
  "${renderer_fixed_backend}/render_pass.cpp" COPYONLY)
foreach(kind Color Input)
  string(TOLOWER "${kind}" name)
  cupid_renderer_replace("${renderer_fixed_backend}/render_pass.cpp"
    "\t\tmemcpy(subpass_info.${name}_attachments, subpass.p${kind}Attachments,"
    "\t\tif (subpass.${name}AttachmentCount)\n\t\t\tmemcpy(subpass_info.${name}_attachments, subpass.p${kind}Attachments,")
endforeach()

cupid_renderer_replace("${renderer_state_sources}/rdp_renderer.cpp"
  "0x01010101 * constants.prim_lod_frac"
  "0x01010101u * constants.prim_lod_frac")

# Keep interlaced scanout at native field height after full downsampling.
cupid_renderer_replace("${renderer_state_sources}/video_interface.cpp"
  "is_final_pass = !serrate || !options.upscale_deinterlacing;"
  "is_final_pass = !serrate || !options.upscale_deinterlacing || (scaling_factor >> downscale_steps) <= 1;")
